#include "core/IR.h"

#include "core/DiagnosticEmitter.h"
#include "util/ADT.h"

#include <QtGlobal>
#include <QObject>
#include <QQueue>
#include <QDebug>

namespace{
const char ILLEGAL_CHARS_1[] = {
    '.', '[', ']', '(', ')', '<', '>', '\\', '/', '+', '=', '*', '~', '`', '\'', '"', ',', '?', '@', '#', '$', '%', '^', '&', '|', ':', ';', ' '
};

struct EscapedIllegalCharRecord{
    char c;
    char escapeChar;
};

const EscapedIllegalCharRecord ILLEGAL_CHARS_2[] = {
    {'\t', 't'},
    {'\n', 'n'},
    {'\r', 'r'},
    {'\f', 'f'},
    {'\a', 'a'},
    {'\b', 'b'},
    {'\0', '0'}
};
}

bool IRNodeType::validateName(DiagnosticEmitterBase& diagnostic, const QString &name)
{
    // any character sequence is allowed, except ones in ILLEGAL_CHARS
    bool isValid = true;
    int len = name.length();
    if(Q_UNLIKELY(len == 0)){
        diagnostic(Diag::Error_BadName_EmptyString);
        return false;
    }
    for(int i = 0, len = sizeof(ILLEGAL_CHARS_1)/sizeof(char); i < len; ++i){
        QChar c(ILLEGAL_CHARS_1[i]);
        if(Q_UNLIKELY(name.contains(c, Qt::CaseInsensitive))){
            diagnostic(Diag::Error_BadName_IllegalChar, QString(c), name);
            isValid = false;
        }
    }
    for(int i = 0, len = sizeof(ILLEGAL_CHARS_2)/sizeof(decltype(ILLEGAL_CHARS_2[0])); i < len; ++i){
        QChar c(ILLEGAL_CHARS_2[i].c);
        if(Q_UNLIKELY(name.contains(c, Qt::CaseInsensitive))){
            QString charStr('\'');
            charStr.append(ILLEGAL_CHARS_2[i].escapeChar);
            diagnostic(Diag::Error_BadName_IllegalChar, charStr, name);
            isValid = false;
        }
    }
    for(const auto& c: name){
        if(Q_UNLIKELY(!c.isPrint())){
            diagnostic(Diag::Error_BadName_UnprintableChar);
            isValid = false;
            break;
        }
    }
    bool isPureNumber = false;
    // check if it is a number, even if it overflows
    int num = static_cast<int>(name.toLongLong(&isPureNumber));
    if(Q_UNLIKELY(isPureNumber)){
        diagnostic(Diag::Error_BadName_PureNumber, name, num);
        isValid = false;
    }

    return isValid;
}

bool  IRNodeType::validate(DiagnosticEmitterBase& diagnostic, IRRootType &root)
{
    bool isValidated = true;

    // check if name of this node is valid
    bool isNameValid = validateName(diagnostic, name);
    isValidated = isNameValid;

    // if name is valid, update it to diagnostic
    if(Q_LIKELY(isNameValid)){
        diagnostic.setDetailedName(name);
    }

    // should never happen during correct execution
    Q_ASSERT(parameterList.size() == parameterNameList.size());

    parameterNameToIndex.clear();
    // check all parameters
    for(int i = 0, len = parameterNameList.size(); i < len; ++i){
        DiagnosticPathNode dnode(diagnostic, tr("Parameter %1").arg(i));
        const QString& paramName = parameterNameList.at(i);
        // check if parameter names are valid
        bool isNameValid = validateName(diagnostic, paramName);
        if(Q_LIKELY(isNameValid)){
            dnode.setDetailedName(paramName);
        }
        isValidated = isValidated && isNameValid;
        // check if parameter type is valid
        if(Q_UNLIKELY(!isValidIRValueType(getParameterType(i)))){
            diagnostic(Diag::Error_IR_BadType_BadTypeForNodeParam, paramName, getParameterType(i));
            isValidated = false;
        }
        // check if there is a name clash
        auto iter = parameterNameToIndex.find(paramName);
        if(Q_LIKELY(iter == parameterNameToIndex.end())){
            parameterNameToIndex.insert(paramName, i);
        }else{
            diagnostic(Diag::Error_IR_NameClash_NodeParam, paramName, iter.value(), i);
            isValidated = false;
        }
        // we do not implement other checks (e.g. parameter value constraint) yet
        dnode.pop();
    }

    // check primary key
    if(primaryKeyName.isEmpty()){
        primaryKeyIndex = -1;
    }else{
        primaryKeyIndex = getParameterIndex(primaryKeyName);
        if(Q_UNLIKELY(primaryKeyIndex == -1)){
            diagnostic(Diag::Error_IR_BadPrimaryKey_KeyNotFound, primaryKeyName);
            isValidated = false;
        }else if(Q_UNLIKELY(!(parameterList.at(primaryKeyIndex).isUnique))){
            diagnostic(Diag::Error_IR_BadPrimaryKey_KeyNotUnique, primaryKeyName);
            isValidated = false;
        }
    }

    // check children
    childNodeNameToIndex.clear();
    for(int i = 0, len = childNodeList.size(); i < len; ++i){
        const QString& str = childNodeList.at(i);
        int index = root.getNodeTypeIndex(str);
        if(Q_UNLIKELY(index == -1)){
            diagnostic(Diag::Error_IR_BadReference_ChildNodeType, str);
            isValidated = false;
        }else{
            auto iter = childNodeNameToIndex.find(str);
            if(Q_LIKELY(iter == childNodeNameToIndex.end())){
                childNodeNameToIndex.insert(str, i);
            }else{
                diagnostic(Diag::Error_IR_DuplicatedReference_ChildNodeType, str);
                isValidated = false;
            }
        }
    }
    return isValidated;
}

bool IRRootType::validate(DiagnosticEmitterBase& diagnostic)
{
    isValidated = true;

    if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, name))){
        isValidated = false;
    }

    nodeNameToIndex.clear();
    for(int i = 0, len = nodeList.size(); i < len; ++i){
        QString name = nodeList.at(i).getName();
        auto iter = nodeNameToIndex.find(name);
        if(Q_LIKELY(iter == nodeNameToIndex.end())){
            nodeNameToIndex.insert(name, i);
        }else{
            diagnostic(Diag::Error_IR_NameClash_NodeType, name);
            isValidated = false;
        }
    }
    if(rootNodeName.isEmpty()){
        rootNodeIndex = -1;
    }else{
        rootNodeIndex = getNodeTypeIndex(rootNodeName);
        if(Q_UNLIKELY(rootNodeIndex == -1)){
            diagnostic(Diag::Error_IR_BadReference_RootNodeType, rootNodeName);
            isValidated = false;
        }
    }
    // must happen after nodeNameToIndex is properly initialized
    for(int i = 0, len = nodeList.size(); i < len; ++i){
        DiagnosticPathNode dnode(diagnostic, tr("Node Type %1").arg(i));
        // no short circuit
        isValidated = nodeList[i].validate(diagnostic, *this) && isValidated;
        dnode.pop();
    }
    return isValidated;
}

bool IRNodeInstance::validate(DiagnosticEmitterBase& diagnostic, IRRootInstance& root)
{
    // note that broken tree and invalid node type should already been catched in IRRootInstance::validate()
    // therefore here it is safe to assume that type index is valid and tree structure is well formed

    bool isValidated = true;
    const IRRootType& rootTy = root.getType();
    const IRNodeType& ty = rootTy.getNodeType(typeIndex);
    diagnostic.setDetailedName(ty.getName());

    // check if parameter is good
    if(Q_UNLIKELY(ty.getNumParameter() != parameters.size())){
        diagnostic(Diag::Error_IR_BadParameterList_Count, ty.getNumParameter(), parameters.size());
        isValidated = false;
    }else{
        // number of parameters match
        // check if their type is matching
        for(int i = 0, len = parameters.size(); i < len; ++i){
            const QVariant& val = parameters.at(i);
            ValueType valTy = ty.getParameterType(i);
            ValueType givenTy = getValueType(static_cast<QMetaType::Type>(val.userType()));
            if(Q_UNLIKELY(valTy != givenTy)){
                diagnostic(Diag::Error_IR_BadParameterList_Type, i, valTy, givenTy);
                isValidated = false;
            }
        }
    }

    // check if children are good
    int numChildNodeType = ty.getNumChildNode();
    RunTimeSizeArray<bool> isChildTypeGood(static_cast<std::size_t>(numChildNodeType), true);

    childNodeTypeIndexToLocalIndex.clear();
    for(int i = 0; i < numChildNodeType; ++i){
        int globalNodeTyIndex = rootTy.getNodeTypeIndex(ty.getChildNodeName(i));
        childNodeTypeIndexToLocalIndex.insert(globalNodeTyIndex, i);
    }
    childTypeList.clear();
    childTypeList.reserve(numChildNodeType);
    for(int i = 0; i < numChildNodeType; ++i){
        childTypeList.push_back(ChildTypeRecord());
    }
    Q_ASSERT(childTypeList.size() == numChildNodeType);

    // when checking whether children are good,
    // fatal errors are tracked by isValidated
    // non-fatal errors are tracked by isChildTypeGood
    for(int i = 0, cnt = childNodeList.size(); i < cnt; ++i){
        int childNodeIndex = childNodeList.at(i);
        DiagnosticPathNode dnode(diagnostic, tr("Child %1").arg(i));
        IRNodeInstance& child = root.getNode(childNodeIndex);
        bool isChildGood = child.validate(diagnostic, root);
        int localTyIndex = getLocalTypeIndex(child.getTypeIndex());
        if(Q_UNLIKELY(localTyIndex == -1)){
            diagnostic(Diag::Error_IR_BadTree_UnexpectedChild, rootTy.getNodeType(child.getTypeIndex()).getName());
            isValidated = false;
        }else{
            Q_ASSERT(localTyIndex >= 0 && localTyIndex < childTypeList.size());
            childTypeList[localTyIndex].nodeList.push_back(childNodeIndex);
            if(Q_UNLIKELY(!isChildGood)){
                isChildTypeGood.at(localTyIndex) = false;
            }
        }
        dnode.pop();
    }
    // check for key unique constraints and construct perParamHash in childTypeList
    // do not check this property if anything is already failed
    if(isValidated){
        for(int i = 0; i < numChildNodeType; ++i){
            if(Q_LIKELY(isChildTypeGood.at(i))){
                ChildTypeRecord& record = childTypeList[i];
                record.perParamHash.clear();
                const IRNodeType& nodeTy = rootTy.getNodeType(rootTy.getNodeTypeIndex(ty.getChildNodeName(i)));
                for(int i = 0, numParam = nodeTy.getNumParameter(); i < numParam; ++i){
                    record.perParamHash.push_back(QHash<QVariant,int>());
                    if(nodeTy.getParameterIsUnique(i)){
                        QHash<QVariant,int>& hash = record.perParamHash.back();
                        for(int nodeIndex : record.nodeList){
                            const IRNodeInstance& inst = root.getNode(nodeIndex);
                            const QVariant& val = inst.parameters.at(i);
                            auto iter = hash.find(val);
                            if(Q_LIKELY(iter == hash.end())){
                                hash.insert(val, nodeIndex);
                            }else{
                                diagnostic(Diag::Error_IR_BadTree_BrokenConstraint_ParamNotUnique,
                                           nodeTy.getName(), nodeTy.getParameterName(i), iter.value(), nodeIndex, val.toString());
                                isValidated = false;
                            }
                        }
                    }
                }
                Q_ASSERT(record.perParamHash.size() == nodeTy.getNumParameter());
            }else{ // isChildTypeGood.at(i) == false
                isValidated = false;
            }
        }
    }

    return isValidated;
}

bool IRRootInstance::validate(DiagnosticEmitterBase& diagnostic)
{
    DiagnosticPathNode dnode(diagnostic, tr("Root"));
    if(nodeList.empty()){
        diagnostic(Diag::Error_IR_BadTree_EmptyTree);
        isValidated = false;
        return isValidated;
    }

    isValidated = true;

    // first of all, do a reachability analysis
    RunTimeSizeArray<int> isNodeReachable(static_cast<std::size_t>(nodeList.size()), -2);
    struct Entry{
        int parent;
        int child;
    };

    QQueue<Entry> pendingNodes;
    pendingNodes.enqueue(Entry{-1,0});
    while(!pendingNodes.empty()){
        Entry curEntry = pendingNodes.dequeue();
        int parentIndex = curEntry.parent;
        int currentIndex = curEntry.child;
        Q_ASSERT(currentIndex < nodeList.size());
        bool isCurrentNodeGood = true;

        if(Q_UNLIKELY(isNodeReachable.at(currentIndex) != -2)){
            diagnostic(Diag::Error_IR_BadTree_DuplicatedReference_ChildNode,
                       currentIndex, isNodeReachable.at(currentIndex), parentIndex);
            isCurrentNodeGood = false;
        }else{
            isNodeReachable.at(currentIndex) = parentIndex;
        }

        if(Q_UNLIKELY(currentIndex <= parentIndex)){
            diagnostic(Diag::Error_IR_BadTree_BadNodeOrder, currentIndex, parentIndex);
            isCurrentNodeGood = false;
        }

        const IRNodeInstance& child = nodeList.at(currentIndex);
        if(Q_UNLIKELY(child.getParentIndex() != parentIndex)){
            diagnostic(Diag::Error_IR_BadTree_ConflictingParentReference, currentIndex, child.getParentIndex(), parentIndex);
            isCurrentNodeGood = false;
        }
        if(Q_UNLIKELY(!ty.isNodeTypeIndexValid(child.getTypeIndex()))){
            diagnostic(Diag::Error_IR_BadTree_BadNodeTypeIndex, currentIndex, child.getTypeIndex());
            isCurrentNodeGood = false;
        }

        isValidated = isValidated && isCurrentNodeGood;
        if(Q_LIKELY(isCurrentNodeGood)){
            for(int i = 0, num = child.getNumChildNode(); i < num; ++i){
                int childIndex = child.getChildNodeByOrder(i);
                pendingNodes.enqueue(Entry{currentIndex, childIndex});
            }
        }
    }
    for(int i = 0, num = nodeList.size(); i < num; ++i){
        if(Q_UNLIKELY(isNodeReachable.at(i) == -2)){
            diagnostic(Diag::Error_IR_BadTree_UnreachableNode, i);
            isValidated = false;
        }
    }

    if(Q_LIKELY(isValidated)){
        isValidated = nodeList.front().validate(diagnostic, *this);
    }

    dnode.pop();
    return isValidated;
}
