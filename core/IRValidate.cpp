#include "core/IR.h"

#include "core/DiagnosticEmitter.h"
#include "util/ADT.h"

#include <QtGlobal>
#include <QObject>
#include <QQueue>

namespace{
const char ILLEGAL_CHARS_1[] = {
    '.', '[', ']', '(', ')', '<', '>', '\\', '/', '=', '*', '~', '`', '\'', '"', ',', '?', '@', '#', '$', '%', '^', '&', '|', ':', ';', ' '
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

bool IRNodeType::validateMemberName(DiagnosticEmitterBase& diagnostic, QString name)
{
    // any character sequence is allowed, except ones in ILLEGAL_CHARS
    bool isValid = true;
    QString errorCategory = tr("Invalid name");
    int len = name.length();
    if(Q_UNLIKELY(len == 0)){
        diagnostic.error(errorCategory, tr("Name is empty"));
        return false;
    }
    for(int i = 0, len = sizeof(ILLEGAL_CHARS_1)/sizeof(char); i < len; ++i){
        QChar c(ILLEGAL_CHARS_1[i]);
        if(Q_UNLIKELY(name.contains(c, Qt::CaseInsensitive))){
            diagnostic.error(errorCategory,
                             tr("Name contains illegal character '%1'").arg(QString(c)),
                             name);
            isValid = false;
        }
    }
    for(int i = 0, len = sizeof(ILLEGAL_CHARS_2)/sizeof(char); i < len; ++i){
        QChar c(ILLEGAL_CHARS_2[i].c);
        if(Q_UNLIKELY(name.contains(c, Qt::CaseInsensitive))){
            diagnostic.error(errorCategory,
                             tr("Name contains illegal character '\\%1'").arg(QString(ILLEGAL_CHARS_2[i].escapeChar)),
                             name);
            isValid = false;
        }
    }
    for(const auto& c: name){
        if(Q_UNLIKELY(!c.isPrint())){
            diagnostic.error(errorCategory,
                             tr("Name contains unprintable character"));
            isValid = false;
            break;
        }
    }
    return isValid;
}

bool  IRNodeType::validate(DiagnosticEmitterBase& diagnostic, IRRootType &root)
{
    bool isValidated = true;

    // check if name of this node is valid
    bool isNameValid = validateMemberName(diagnostic, name);
    isValidated = isNameValid;

    // if name is valid, update it to diagnostic
    if(Q_LIKELY(isNameValid)){
        diagnostic.attachDescriptiveName(name);
    }

    // should never happen during correct execution
    if(Q_UNLIKELY(parameterList.size() != parameterNameList.size())){
        diagnostic.error(QString(),
                       tr("inconsistent parameter count (%1, %2)").arg(
                           QString::number(parameterList.size()),
                           QString::number(parameterNameList.size())
                         )
                       );
        return false;
    }

    parameterNameToIndex.clear();
    // check all parameters
    for(int i = 0, len = parameterNameList.size(); i < len; ++i){
        diagnostic.pushNode(tr(".%2([%1])").arg(i));
        const QString& paramName = parameterNameList.at(i);
        // check if parameter names are valid
        bool isNameValid = validateMemberName(diagnostic, paramName);
        if(Q_LIKELY(isNameValid)){
            diagnostic.attachDescriptiveName(paramName);
        }
        isValidated = isValidated && isNameValid;
        // check if parameter type is valid
        if(Q_UNLIKELY(!isValidIRValueType(getParameterType(i)))){
            diagnostic.error(tr("Invalid type"),
                             tr("Parameter type (%1) is invalid").arg(getTypeNameString(getParameterType(i))));
            isValidated = false;
        }
        // check if there is a name clash
        auto iter = parameterNameToIndex.find(paramName);
        if(Q_LIKELY(iter == parameterNameToIndex.end())){
            parameterNameToIndex.insert(paramName, i);
        }else{
            diagnostic.error(tr("Name conflict"),
                             tr("parameter name conflict with another one ([%1])").arg(iter.value()));
            isValidated = false;
        }
        // we do not implement other checks (e.g. parameter value constraint) yet
        diagnostic.popNode();
    }

    // check primary key
    if(primaryKeyName.isEmpty()){
        primaryKeyIndex = -1;
    }else{
        primaryKeyIndex = getParameterIndex(primaryKeyName);
        if(Q_UNLIKELY(primaryKeyIndex == -1)){
            diagnostic.error(tr("Invalid name reference"),
                             tr("Specified primary key parameter do not exist"),
                             primaryKeyName);
            isValidated = false;
        }else if(Q_UNLIKELY(!(parameterList.at(primaryKeyIndex).isUnique))){
            diagnostic.error(tr("Invalid constraint"),
                             tr("Primary key must be unique"),
                             primaryKeyName);
            isValidated = false;
        }
    }

    // check children
    childNodeNameToIndex.clear();
    for(int i = 0, len = childNodeList.size(); i < len; ++i){
        const QString& str = childNodeList.at(i);
        int index = root.getNodeIndex(str);
        if(Q_UNLIKELY(index == -1)){
            diagnostic.error(tr("Invalid name reference"),
                             tr("Specified child node name do not exist"),
                             str);
            isValidated = false;
        }else{
            auto iter = childNodeNameToIndex.find(str);
            if(Q_LIKELY(iter == childNodeNameToIndex.end())){
                childNodeNameToIndex.insert(str, i);
            }else{
                diagnostic.error(tr("Duplicated reference"),
                                 tr("Child node name referenced more than once"),
                                 str);
                isValidated = false;
            }
        }
    }
    return isValidated;
}

bool IRRootType::validate(DiagnosticEmitterBase& diagnostic)
{
    isValidated = true;

    if(Q_UNLIKELY(!IRNodeType::validateMemberName(diagnostic, name))){
        diagnostic.error(tr("Invalid name"),
                         tr("IR name is invalid"),
                         name);
        isValidated = false;
    }

    nodeNameToIndex.clear();
    for(int i = 0, len = nodeList.size(); i < len; ++i){
        QString name = nodeList.at(i).getName();
        auto iter = nodeNameToIndex.find(name);
        if(Q_LIKELY(iter == nodeNameToIndex.end())){
            nodeNameToIndex.insert(name, i);
        }else{
            diagnostic.error(tr("Name conflict"),
                             tr("More than one node using the same name"),
                             name);
            isValidated = false;
        }
    }
    if(rootNodeName.isEmpty()){
        rootNodeIndex = -1;
    }else{
        rootNodeIndex = getNodeIndex(rootNodeName);
        if(Q_UNLIKELY(rootNodeIndex == -1)){
            diagnostic.error(tr("Invalid name reference"),
                             tr("Specified root node name do not exist"),
                             rootNodeName);
            isValidated = false;
        }
    }
    // must happen after nodeNameToIndex is properly initialized
    for(int i = 0, len = nodeList.size(); i < len; ++i){
        diagnostic.pushNode(tr("[%1]%2").arg(i));
        // no short circuit
        isValidated = nodeList[i].validate(diagnostic, *this) && isValidated;
        diagnostic.popNode();
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
    diagnostic.attachDescriptiveName(ty.getName());

    // check if parameter is good
    if(Q_UNLIKELY(ty.getNumParameter() != parameters.size())){
        diagnostic.error(tr("Invalid parameter list"),
                         tr("Expecting %1 parameter(s) while getting %2").arg(
                             QString::number(ty.getNumParameter()),
                             QString::number(parameters.size())
                           )
                         );
        isValidated = false;
    }else{
        // number of parameters match
        // check if their type is matching
        for(int i = 0, len = parameters.size(); i < len; ++i){
            const QVariant& val = parameters.at(i);
            ValueType valTy = ty.getParameterType(i);
            if(Q_UNLIKELY(getQMetaType(valTy) != val.userType())){
                diagnostic.error(tr("Parameter type mismatch"),
                                 tr("Parameter %1 is not in expected type (%2)").arg(
                                     ty.getParameterName(i),
                                     getTypeNameString(valTy)
                                   )
                                 );
                isValidated = false;
            }
        }
    }

    // check if children are good
    int numChildNodeType = ty.getNumChildNode();
    RunTimeSizeArray<bool> isChildTypeGood(static_cast<std::size_t>(numChildNodeType), true);

    childNodeTypeIndexToLocalIndex.clear();
    for(int i = 0; i < numChildNodeType; ++i){
        int globalNodeTyIndex = rootTy.getNodeIndex(ty.getChildNodeName(i));
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
        diagnostic.pushNode(tr("/[%1]%2").arg(i));
        IRNodeInstance& child = root.getNode(childNodeIndex);
        bool isChildGood = child.validate(diagnostic, root);
        int localTyIndex = getLocalTypeIndex(child.getTypeIndex());
        if(Q_UNLIKELY(localTyIndex == -1)){
            diagnostic.error(tr("Type Mismatch"),
                             tr("Illegal child node type (%1)").arg(
                                 rootTy.getNodeType(child.getTypeIndex()).getName()
                               )
                             );
            isValidated = false;
        }else{
            Q_ASSERT(localTyIndex >= 0 && localTyIndex < childTypeList.size());
            childTypeList[localTyIndex].nodeList.push_back(childNodeIndex);
            if(Q_UNLIKELY(!isChildGood)){
                isChildTypeGood.at(localTyIndex) = false;
            }
        }
        diagnostic.popNode();
    }
    // check for key unique constraints and construct perParamHash in childTypeList
    // do not check this property if anything is already failed
    if(isValidated){
        for(int i = 0; i < numChildNodeType; ++i){
            if(Q_LIKELY(isChildTypeGood.at(i))){
                ChildTypeRecord& record = childTypeList[i];
                record.perParamHash.clear();
                const IRNodeType& nodeTy = rootTy.getNodeType(rootTy.getNodeIndex(ty.getChildNodeName(i)));
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
                                diagnostic.error(tr("Broken constraint"),
                                                 tr("Child node (%1) has parameter (%2) set as unique,"
                                                    " but duplicate is found (first: %3, current: %4)").arg(
                                                     nodeTy.getName(),
                                                     nodeTy.getParameterName(i),
                                                     QString::number(iter.value()),
                                                     QString::number(nodeIndex)
                                                    ),
                                                 val.toString()
                                                 );
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
    diagnostic.pushNode(tr("~"));
    QString errorCategory = tr("Broken tree");
    if(nodeList.empty()){
        diagnostic.error(errorCategory,
                         tr("Tree is empty"));
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
            diagnostic.error(errorCategory,
                             tr("Node (%1) is referenced more than once (first from %2, current from %3)").arg(
                                 QString::number(currentIndex),
                                 QString::number(isNodeReachable.at(currentIndex)),
                                 QString::number(parentIndex)
                               )
                             );
            isCurrentNodeGood = false;
        }else{
            isNodeReachable.at(currentIndex) = parentIndex;
        }

        if(Q_UNLIKELY(currentIndex <= parentIndex)){
            diagnostic.error(errorCategory,
                             tr("Child node has smaller index (%1) than parent (%2)").arg(
                                 QString::number(currentIndex),
                                 QString::number(parentIndex)
                               )
                             );
            isCurrentNodeGood = false;
        }

        const IRNodeInstance& child = nodeList.at(currentIndex);
        if(Q_UNLIKELY(child.getParentIndex() != parentIndex)){
            diagnostic.error(errorCategory,
                             tr("Parent index from child node (%1) do not match with parent node index (%2)").arg(
                                 QString::number(child.getParentIndex()),
                                 QString::number(parentIndex)
                               )
                             );
            isCurrentNodeGood = false;
        }
        if(Q_UNLIKELY(!ty.isNodeTypeIndexValid(child.getTypeIndex()))){
            diagnostic.error(tr("Invalid node type"),
                             tr("Node (%1) type index (%2) is invalid").arg(
                                 QString::number(currentIndex),
                                 QString::number(child.getTypeIndex())
                               )
                             );
            isCurrentNodeGood = false;
        }

        isValidated = isValidated && isCurrentNodeGood;
        if(Q_LIKELY(isCurrentNodeGood)){
            for(int child : child.childNodeList){
                pendingNodes.enqueue(Entry{currentIndex, child});
            }
        }
    }
    for(int i = 0, num = nodeList.size(); i < num; ++i){
        if(Q_UNLIKELY(isNodeReachable.at(i) == -2)){
            diagnostic.error(errorCategory,
                             tr("Node (%1) is unreachable").arg(i));
            isValidated = false;
        }
    }

    if(Q_LIKELY(isValidated)){
        isValidated = nodeList.front().validate(diagnostic, *this);
    }

    diagnostic.popNode();
    return isValidated;
}
