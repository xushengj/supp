#ifndef IR_H
#define IR_H

#include <QCoreApplication>
#include <QVariant>
#include <QString>
#include <QList>
#include <QStringList>
#include <QHash>

#include "core/Value.h"

class DiagnosticEmitterBase;

class IRNodeType;
class IRRootType;
class IRNodeInstance;
class IRRootInstance;

class IRNodeType{
    Q_DECLARE_TR_FUNCTIONS(IRNodeType)

public:

    explicit IRNodeType(QString name)
        : name(name)
    {}
    //-------------------------------------------------------------------------
    // const interface

    QString getName()     const {return name;}

    int getNumParameter() const {return parameterList.size();}
    int getNumChildNode() const {return childNodeList.size();}

    const QString& getChildNodeName(int index) const {return childNodeList.at(index);}

    QString     getParameterName    (int parameterIndex) const {return parameterNameList.at(parameterIndex);}
    ValueType   getParameterType    (int parameterIndex) const {return parameterList.at(parameterIndex).paramType;}
    bool        getParameterIsUnique(int parameterIndex) const {return parameterList.at(parameterIndex).isUnique;}

    int getPrimaryKeyParameterIndex()const{return primaryKeyIndex;}

    int getParameterIndex    (const QString& name) const{return parameterNameToIndex.value(name, -1);}
    int getChildNodeTypeIndex(const QString& name) const{return childNodeNameToIndex.value(name, -1);}

    bool operator==(const IRNodeType& rhs) const{
        return (this == &rhs) ||
               (name                == rhs.name                 &&
                parameterList       == rhs.parameterList        &&
                parameterNameList   == rhs.parameterNameList    &&
                childNodeList       == rhs.childNodeList);
    }

    //-------------------------------------------------------------------------

    void addChildNode(QString childNodeName)    {childNodeList.push_back(childNodeName);}
    void setPrimaryKey(QString paramName)       {primaryKeyName = paramName;}

    void addParameter(QString name, ValueType paramType, bool isUnique){
        parameterNameList.push_back(name);
        Parameter param;
        param.paramType = paramType;
        param.isUnique = isUnique;
        parameterList.push_back(param);
    }

    bool validate(DiagnosticEmitterBase& diagnostic, IRRootType& root);

    static bool validateName(DiagnosticEmitterBase& diagnostic, const QString& name);

private:
    struct Parameter{
        ValueType paramType;
        bool isUnique;
        bool operator==(const Parameter& rhs)const{
            return paramType == rhs.paramType && isUnique == rhs.isUnique;
        }
    };
    QString name;

    int primaryKeyIndex = -1;
    QString primaryKeyName;

    QList<Parameter> parameterList;
    QStringList parameterNameList;
    QStringList childNodeList;

    // constructed during validate()
    QHash<QString, int> parameterNameToIndex;
    QHash<QString, int> childNodeNameToIndex; // index into childNodeList
};

class IRRootType
{
    Q_DECLARE_TR_FUNCTIONS(IRRootType)

public:
    explicit IRRootType(const QString& name)
        : name(name)
    {}
    // no copy or move because this class would be taken reference by others
    IRRootType(const IRRootType&) = delete;
    IRRootType(IRRootType&&) = delete;

    //-------------------------------------------------------------------------
    // const interface

    const QString& getName() const {return name;}

    int getNodeTypeIndex(const QString& nodeName) const {return nodeNameToIndex.value(nodeName, -1);}

    int  getNumNodeType()                const {return nodeList.size();}
    bool isNodeTypeIndexValid(int index) const {return (index >= 0) && (index < nodeList.size());}

    const IRNodeType&   getNodeType (int index)               const {return nodeList.at(index);}
    const IRNodeType&   getNodeType (const QString& nodeName) const {return nodeList.at(getNodeTypeIndex(nodeName));}

    bool operator==(const IRRootType& rhs) const {
        return  nodeList        == rhs.nodeList &&
                rootNodeName    == rhs.rootNodeName;
    }

    //-------------------------------------------------------------------------

    void addNodeTypeDefinition  (const IRNodeType& node)    {isValidated = false; nodeList.push_back(node);}
    void setRootNodeType        (const QString& nodeName)   {isValidated = false; rootNodeName = nodeName;}

    bool validated() const {return isValidated;}
    bool validate(DiagnosticEmitterBase& diagnostic);

private:
    QString name;
    QList<IRNodeType> nodeList;
    QString rootNodeName;

    QHash<QString, int> nodeNameToIndex;
    int rootNodeIndex = -1;
    bool isValidated  = false;
};

class IRNodeInstance
{
    Q_DECLARE_TR_FUNCTIONS(IRNodeInstance)

public:
    explicit IRNodeInstance(int typeIndex, int nodeIndex)
        : typeIndex(typeIndex), nodeIndex(nodeIndex){}

    //-------------------------------------------------------------------------
    // const interface

    const QVariant& getParameter(int parameterIndex)    const {return parameters.at(parameterIndex);}

    int getTypeIndex()                                  const {return typeIndex;}
    int getParentIndex()                                const {return parentIndex;}
    int getLocalTypeIndex       (int tyIndex)           const {return childNodeTypeIndexToLocalIndex.value(tyIndex, -1);}

    int getNumChildNode()                               const {return childNodeList.size();}
    int getChildNodeByOrder     (int nodeIndex)         const {return childNodeList.at(nodeIndex);}
    int getNumChildNodeUnderType(int nodeLocalTypeIndex)const {return childTypeList.at(nodeLocalTypeIndex).nodeList.size();}

    int getChildNodeIndex(int nodeLocalTypeIndex, int nodeParamIndex, const QVariant& key)const{
        return childTypeList.at(nodeLocalTypeIndex).perParamHash.at(nodeParamIndex).value(key, -1);
    }
    int getChildNodeIndex(int nodeLocalTypeIndex, int nodeIndexUnderType)const{
        return childTypeList.at(nodeLocalTypeIndex).nodeList.at(nodeIndexUnderType);
    }

    //-------------------------------------------------------------------------

    void addChildNode   (int childIndex)                    {childNodeList.append(childIndex);}
    void setParent      (int index)                         {parentIndex = index;}
    void setParameters  (const QList<QVariant>& parameters) {this->parameters = parameters;}

    bool validate(DiagnosticEmitterBase& diagnostic, IRRootInstance& root);

private:
    struct ChildTypeRecord{
        QList<QHash<QVariant, int>> perParamHash;   //!< lazily constructed hash table; [paramIndex][unique'd param value] -> [child node]
        QList<int> nodeList;                        //!< node index list
    };

    const int typeIndex;
    const int nodeIndex;
    int parentIndex   = -1;
    QList<QVariant> parameters;
    QList<int> childNodeList;

    // constructed during validate()
    QHash<int, int> childNodeTypeIndexToLocalIndex; // childnode.getTypeIndex() --> ty.childIndex
    QList<ChildTypeRecord> childTypeList;// one per child node type
};

class IRRootInstance
{
    Q_DECLARE_TR_FUNCTIONS(IRRootInstance)
    friend class IRNodeInstance;
public:
    explicit IRRootInstance(const IRRootType& ty): ty(ty){Q_ASSERT(ty.validated());}

    // no copy or move because this class would be taken reference by others
    IRRootInstance(const IRRootInstance&) = delete;
    IRRootInstance(IRRootInstance&&) = delete;

    //-------------------------------------------------------------------------
    // const interface

    int                     getNumNode()            const {return nodeList.size();}
    const IRNodeInstance&   getNode(int nodeIndex)  const {return nodeList.at(nodeIndex);}

    const IRRootType&       getType()               const {return ty;}

    //-------------------------------------------------------------------------

    int addNode(int typeIndex){
        isValidated = false;
        int index = nodeList.size();
        nodeList.append(IRNodeInstance(typeIndex, index));
        return index;
    }
    IRNodeInstance& getNode(int nodeIndex){
        isValidated = false;
        if(nodeIndex < 0 || nodeList.size() <= nodeIndex){
            throw std::out_of_range("Invalid Node Index");
        }
        return nodeList[nodeIndex];
    }

    bool validated() const {return isValidated;}
    bool validate(DiagnosticEmitterBase& diagnostic);

private:
    const IRRootType& ty;
    bool isValidated = false;
    QList<IRNodeInstance> nodeList;// must be stored in pre-order; node 0 is root
};

#endif // IR_H
