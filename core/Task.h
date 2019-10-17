#ifndef TASK_H
#define TASK_H

#include <QString>
#include <QStack>

#include <memory>

#include "core/Value.h"
#include "core/Expression.h"

class DiagnosticEmitterBase;
class ExecutionContext;
class IRRootType;

class Task;

enum class StatementType{
    Unreachable,    //!< trigger execution abort
    Assignment,     //!< write to a value reference
    Output,         //!< write result to output
    Call,           //!< call a function
    Return,         //!< function return
    Branch          //!< any branch within function
};

struct Statement{
    StatementType ty;
    int statementIndexInType;
};

// no data for unreachable statement

struct AssignmentStatement{
    int lvalueExprIndex;    //!< expression index of left hand side; -1 for name based assignment
    int rvalueExprIndex;    //!< expression index of right hand side
    QString lvalueName;     //!< name of variable at left hand size; only used if expr index is -1
};

struct OutputStatement{
    int exprIndex;          //!< expression for output
};

struct CallStatement{
    QString functionName;
    QList<int> argumentExprList;
};

struct BranchStatement{
    struct BranchCase{
        int exprIndex;
        int stmtIndex;  //!< statement index to jump to if the expression evaluates to true; -2 if unreachable, -1 if fall through
    };

    int defaultStmtIndex;  //!< stmt to go to if none of the condition applies; -2 if unreachable, -1 if fall through
    QList<BranchCase> cases;
};

// the struct to add branch before label names are resolved
struct BranchStatementTemp{
    enum class BranchActionType{
        Unreachable,
        Fallthrough,
        Jump
    };

    struct BranchCase{
        int exprIndex;
        BranchActionType action;
        QString labelName;
    };

    BranchActionType defaultAction;
    QString defaultJumpLabelName;
    QList<BranchCase> cases;
};

/**
 * @brief The Function class
 */
class Function{
    Q_DECLARE_TR_FUNCTIONS(Function)
public:
    explicit Function(QString name): functionName(name){}
    ~Function(){}
    /**
     * @brief addLocalVariable add a local variable (or function formal argument) definition
     * @param name name of local variable to add
     * @param ty type of local variable
     * @param initializer the initial value of local variable, default value for argument
     */
    void addLocalVariable(const QString& name, ValueType ty, QVariant initializer = QVariant())
    {
        localVariableNames.push_back(name);
        localVariableTypes.push_back(ty);
        localVariableInitializer.push_back(initializer);
    }
    void setParamCount(int cnt){paramCount = cnt;}
    void setRequiredParamCount(int cnt){requiredParamCount = cnt;}

    /**
     * @brief addExpression adds the expression to the function. Function will take over the object ownership
     * @param ptr pointer to expression being passed
     * @return expression index
     */
    int addExpression(ExpressionBase* ptr){
        int index = exprList.size();
        exprList.push_back(ptr);
        return index;
    }

    int addUnreachableStatement(){
        int stmtIndex = stmtList.size();
        stmtList.push_back(Statement{StatementType::Unreachable, -1});
        return stmtIndex;
    }
    int addReturnStatement(){
        int stmtIndex = stmtList.size();
        stmtList.push_back(Statement{StatementType::Return, -1});
        return stmtIndex;
    }

    int addStatement(const AssignmentStatement& stmt){
        int stmtIndex = stmtList.size();
        Statement stmtInst = {
            StatementType::Assignment,
            assignStmtList.size()
        };
        stmtList.push_back(stmtInst);
        assignStmtList.push_back(stmt);
        return stmtIndex;
    }

    int addStatement(const OutputStatement& stmt){
        int stmtIndex = stmtList.size();
        Statement stmtInst = {
            StatementType::Output,
            outputStmtList.size()
        };
        stmtList.push_back(stmtInst);
        outputStmtList.push_back(stmt);
        return stmtIndex;
    }

    int addStatement(const CallStatement& stmt){
        int stmtIndex = stmtList.size();
        Statement stmtInst = {
            StatementType::Call,
            callStmtList.size()
        };
        stmtList.push_back(stmtInst);
        callStmtList.push_back(stmt);
        return stmtIndex;
    }

    int addStatement(const BranchStatementTemp& stmt){
        int stmtIndex = stmtList.size();
        Statement stmtInst = {
            StatementType::Branch,
            branchTempStmtList.size()
        };
        stmtList.push_back(stmtInst);
        branchTempStmtList.push_back(stmt);
        return stmtIndex;
    }

    int addLabel(QString labelName){
        int index = labels.size();
        labels.push_back(labelName);
        labeledStmtIndexList.push_back(stmtList.size());
        return index;
    }

    bool validate(DiagnosticEmitterBase& diagnostic, const Task& task);

    const QString& getName()const{return functionName;}

    // note that function parameter is implicitly a local variable
    int getNumParameter()const {return paramCount;}
    int getNumRequiredParameter()const{return requiredParamCount;}
    int getNumLocalVariable()const{return localVariableNames.size();}
    int getLocalVariableIndex(const QString& varName)const{
        auto iter = localVariableNameToIndex.find(varName);
        if(iter == localVariableNameToIndex.end())
            return -1;
        return iter.value();
    }
    const QString& getLocalVariableName(int localVarIndex)const{return localVariableNames.at(localVarIndex);}
    ValueType getLocalVariableType(int localVarIndex, QString* varNameWBPtr = nullptr)const{
        if(varNameWBPtr)
            *varNameWBPtr = localVariableNames.at(localVarIndex);
        return localVariableTypes.at(localVarIndex);
    }
    const QVariant& getLocalVariableInitializer(int localVarIndex) const{
        return localVariableInitializer.at(localVarIndex);
    }

    int getNumExpression()const{return exprList.size();}
    int getNumStatement()const{return stmtList.size();}

    const ExpressionBase* getExpression(int exprIndex) const {return exprList.at(exprIndex);}
    const Statement& getStatement(int stmtIndex) const {return stmtList.at(stmtIndex);}
    const AssignmentStatement& getAssignmentStatement(int assignStmtIndex) const {return assignStmtList.at(assignStmtIndex);}
    const OutputStatement& getOutputStatement(int outputStmtIndex) const {return outputStmtList.at(outputStmtIndex);}
    const CallStatement& getCallStatement(int callStmtIndex) const {return callStmtList.at(callStmtIndex);}
    const BranchStatement& getBranchStatement(int branchStmtIndex) const {return branchStmtList.at(branchStmtIndex);}

    int getNumLabel()const{return labels.size();}
    int getLabelAddress(int labelIndex, QString* labelNameWBPtr = nullptr) const{
        if(labelNameWBPtr)
            *labelNameWBPtr = labels.at(labelIndex);
        return labeledStmtIndexList.at(labelIndex);
    }

    const QStringList getReferencedFunctions()const{return calledFunctions;}

private:
    ExprList exprList;
    QList<Statement> stmtList;
    // unreachable statement do not have additional data
    QList<AssignmentStatement> assignStmtList;
    QList<OutputStatement> outputStmtList;
    QList<CallStatement> callStmtList;
    QList<BranchStatement> branchStmtList;
    QList<BranchStatementTemp> branchTempStmtList; // one to one conversion to branchStmtList during validate

    QStringList labels;
    QList<int> labeledStmtIndexList;

    int paramCount = 0;
    int requiredParamCount = 0; //!< number of parameters without initializer
    QString functionName;
    QStringList localVariableNames;
    QList<ValueType> localVariableTypes;
    QList<QVariant> localVariableInitializer;

    // constructed during validate()
    QHash<QString, int> localVariableNameToIndex;
    // debug / error checking purpose only
    QStringList calledFunctions;
};


class Task
{
    Q_DECLARE_TR_FUNCTIONS(Task)
public:

    Task(const IRRootType& root);

    // no copy or move because this class would be taken reference by others
    Task(const Task&) = delete;
    Task(Task&&) = delete;

    void addGlobalVariable(QString varName, ValueType ty, QVariant initializer = QVariant()){
        isValidated = false;
        globalVariables.varNameList.push_back(varName);
        globalVariables.varTyList.push_back(ty);
        globalVariables.varInitializerList.push_back(initializer);
    }
    void addNodeMember(int nodeIndex, QString memberName, ValueType ty, QVariant initializer = QVariant()){
        isValidated = false;
        if(Q_UNLIKELY(nodeIndex < 0 || nodeIndex >= nodeMemberDecl.size())){
            throw std::out_of_range("Bad node index");
        }
        MemberDecl& decl = nodeMemberDecl[nodeIndex];
        decl.varNameList.push_back(memberName);
        decl.varTyList.push_back(ty);
        decl.varInitializerList.push_back(initializer);
    }
    int addFunction(const Function& f){
        int index = functions.size();
        functions.push_back(f);
        functionNameToIndex.insert(f.getName(), index);
        return index;
    }

    enum class CallbackType{
        OnEntry,
        OnExit
    };

    void setNodeCallback(int nodeIndex, const QString& functionName, CallbackType ty){
        if(Q_UNLIKELY(nodeIndex < 0 || nodeIndex >= nodeCallbacks.size())){
            throw std::out_of_range("Bad node index");
        }
        NodeCallbackRecord& record = nodeCallbacks.back()[nodeIndex];
        int functionIndex = getFunctionIndex(functionName);
        switch(ty){
        case CallbackType::OnEntry:
            record.onEntryFunctionIndex = functionIndex;
            break;
        case CallbackType::OnExit:
            record.onExitFunctionIndex = functionIndex;
            break;
        }
    }

    int getNodeCallback(int nodeIndex, CallbackType ty, int passIndex)const{
        const NodeCallbackRecord& record = nodeCallbacks.at(passIndex).at(nodeIndex);
        switch(ty){
        case CallbackType::OnEntry:
            return record.onEntryFunctionIndex;
        case CallbackType::OnExit:
            return record.onExitFunctionIndex;
        }
        Q_UNREACHABLE();
    }
    int addNewPass();

    int getNumGlobalVariable()const{
        return globalVariables.varNameList.size();
    }
    int getGlobalVariableIndex(const QString& name)const{
        return globalVariables.getIndex(name);
    }
    const QString& getGlobalVariableName(int index)const{
        return globalVariables.varNameList.at(index);
    }
    ValueType getGlobalVariableType(int index)const{
        return globalVariables.varTyList.at(index);
    }
    const QVariant& getGlobalVariableInitializer(int index)const{
        return globalVariables.varInitializerList.at(index);
    }
    int getNumNodeMember(int nodeTypeIndex)const{
        return nodeMemberDecl.at(nodeTypeIndex).varNameList.size();
    }
    int getNodeMemberIndex(int nodeTypeIndex, const QString& name)const{
        return nodeMemberDecl.at(nodeTypeIndex).getIndex(name);
    }
    const QString& getNodeMemberName(int nodeTypeIndex, int memberIndex)const{
        return nodeMemberDecl.at(nodeTypeIndex).varNameList.at(memberIndex);
    }
    ValueType getNodeMemberType(int nodeTypeIndex, int memberIndex)const{
        return nodeMemberDecl.at(nodeTypeIndex).varTyList.at(memberIndex);
    }
    const QVariant& getNodeMemberInitializer(int nodeTypeIndex, int memberIndex)const{
        return nodeMemberDecl.at(nodeTypeIndex).varInitializerList.at(memberIndex);
    }

    int getNumPass()const{return nodeCallbacks.size();}
    int getNumFunction()const{return functions.size();}
    int getFunctionIndex(const QString& functionName)const{return functionNameToIndex.value(functionName, -1);}
    const Function& getFunction(int functionIndex)const{return functions.at(functionIndex);}

    bool validated() const {return isValidated;}
    bool validate(DiagnosticEmitterBase& diagnostic);

    const IRRootType& getRootType() const {return root;}

private:
    struct MemberDecl{
        QHash<QString, int> varNameToIndex;
        QStringList varNameList;
        QList<ValueType> varTyList;
        QList<QVariant> varInitializerList;
        int getIndex(const QString& name)const{
            auto iter = varNameToIndex.find(name);
            if(Q_UNLIKELY(iter == varNameToIndex.end())){
                return -1;
            }
            return iter.value();
        }
    };
    struct NodeCallbackRecord{
        int onEntryFunctionIndex;
        int onExitFunctionIndex;
    };

    const IRRootType& root;
    bool isValidated = false;
    MemberDecl globalVariables;

    // indexed by nodeIndex as in IRRoot
    QList<MemberDecl> nodeMemberDecl;
    QList<QList<NodeCallbackRecord>> nodeCallbacks;

    QList<Function> functions;
    // constructed during validation
    QHash<QString, int> functionNameToIndex;
};

#endif // TASK_H
