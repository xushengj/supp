#include "core/Task.h"

#include "core/DiagnosticEmitter.h"
#include "core/Expression.h"
#include "core/IR.h"
#include "util/ADT.h"

#include <QQueue>

#include <functional>

Function::~Function()
{
    for(ExpressionBase* ptr : exprList){
        delete ptr;
    }
}

bool Function::validate(DiagnosticEmitterBase& diagnostic, const Task& task)
{
    // POSSIBLE IMPROVEMENT:
    // 1. find unreachable expression
    // 2. find unreachable statement (maybe just statically unreachable statements)
    bool isValidated = true;

    // check if name is good
    if(Q_LIKELY(IRNodeType::validateMemberName(diagnostic, functionName))){
        diagnostic.attachDescriptiveName(functionName);
    }else{
        isValidated = false;
    }

    // check if all expressions are good
    // 1. no circular dependence (just check if any expression is referencing another expression with larger index)
    // 2. type expectation should match
    QString exprCheckFailCommonPrefix = tr("During checking expression %1: dependent expression %2: %3");
    for(int index = 0, len = exprList.size(); index < len; ++index){
        const ExpressionBase* ptr = exprList.at(index);
        QList<int> dependentExprIndices;
        QList<ValueType> dependentExprTypes;
        ptr->getDependency(dependentExprIndices, dependentExprTypes);
        Q_ASSERT(dependentExprIndices.size() == dependentExprTypes.size());
        for(int i = 0, len = dependentExprIndices.size(); i < len; ++i){
            int exprIndex = dependentExprIndices.at(i);
            if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size() || exprIndex >= index)){
                diagnostic.error(tr("Invalid reference"),
                                 exprCheckFailCommonPrefix.arg(
                                     QString::number(index),
                                     QString::number(exprIndex),
                                     tr("index is invalid")
                                   )
                                 );
                isValidated = false;
            }else{
                if(Q_UNLIKELY(ptr->getExpressionType() != dependentExprTypes.at(i))){
                    diagnostic.error(tr("Type mismatch"),
                                     exprCheckFailCommonPrefix.arg(
                                         QString::number(index),
                                         QString::number(exprIndex),
                                         tr("Expecting type %1 but get %2").arg(
                                             getTypeNameString(ptr->getExpressionType()),
                                             getTypeNameString(dependentExprTypes.at(i))
                                         )
                                       )
                                     );
                    isValidated = false;
                }
            }
        }
    }

    // check if all statements are good
    // 1. all referenced expression indices are valid and the expression generates correct type
    // 2. (for branch) all label references are valid
    // we do not check stmtList because it is impossible to be valid if only the addUnreachableStatement() addStatement() is used
    for(const auto& stmt: assignStmtList){
        if(stmt.lvalueExprIndex == -1){
            // check if the name reference is good
            if(Q_UNLIKELY(!IRNodeType::validateMemberName(diagnostic, stmt.lvalueName))){
                isValidated = false;
            }
        }else{
            // lhs should be valueptr
            if(Q_UNLIKELY(stmt.lvalueExprIndex < 0 || stmt.lvalueExprIndex >= exprList.size())){
                diagnostic.error(tr("Invalid reference"),
                                 tr("LHS expression index (%1) of assignment is invalid").arg(
                                     QString::number(stmt.lvalueExprIndex)
                                   )
                                 );
                isValidated = false;
            }else if(Q_UNLIKELY(exprList.at(stmt.lvalueExprIndex)->getExpressionType() != ValueType::ValuePtr)){
                diagnostic.error(tr("Type mismatch"),
                                 tr("LHS expression of type %1 is not writable").arg(
                                     getTypeNameString(exprList.at(stmt.lvalueExprIndex)->getExpressionType())));
                isValidated = false;
            }
        }
        // we do not test rhs type yet
    }
    for(const auto& stmt: outputStmtList){
        // for now we only output text, so output statement should only accept string expression
        // later on we may check output type based on configuration from Task or ExecutionContext
        int exprIndex = stmt.exprIndex;
        if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size())){
            diagnostic.error(tr("Invalid reference"),
                             tr("Expression index (%1) of output statement is invalid").arg(
                                 QString::number(exprIndex)
                               )
                             );
            isValidated = false;
        }else{
            const ExpressionBase* ptr = exprList.at(exprIndex);
            if(Q_UNLIKELY(ptr->getExpressionType() != ValueType::String)){
                diagnostic.error(tr("Type mismatch"),
                                 tr("Output statement expects string type expression but got %1").arg(
                                     getTypeNameString(ptr->getExpressionType())
                                   )
                                 );
                isValidated = false;
            }
        }
    }
    calledFunctions.clear();
    QHash<QString, int> calledFunctionNameToIndex; // actually just use it as a set
    for(const auto& stmt: callStmtList){
        int functionIndex = task.getFunctionIndex(stmt.functionName);
        if(Q_UNLIKELY(functionIndex == -1)){
            diagnostic.error(tr("Function not found"),
                             tr("Callee function is not found in current task"),
                             stmt.functionName);
            isValidated = false;
        }else{
            const Function& f = task.getFunction(functionIndex);
            const QString& functionName = f.getName();
            if(calledFunctionNameToIndex.count(functionName) == 0){
                calledFunctionNameToIndex.insert(functionName, calledFunctions.size());
                calledFunctions.push_back(functionName);
            }
            int paramPassed = stmt.argumentExprList.size();
            int paramCount = f.getNumParameter();
            int requiredParamCount = f.getNumRequiredParameter();
            if(Q_UNLIKELY(paramPassed < requiredParamCount)){
                diagnostic.error(tr("Bad parameter list"),
                                 tr("Function expects at least %1 argument but got %2").arg(
                                     QString::number(requiredParamCount),
                                     QString::number(paramPassed)
                                   ),
                                 stmt.functionName);
                isValidated = false;
            }else if(Q_UNLIKELY(paramPassed > paramCount)){
                diagnostic.error(tr("Bad parameter list"),
                                 tr("Function expects at most %1 input but got %2").arg(
                                     QString::number(paramCount),
                                     QString::number(paramPassed)
                                   ),
                                 stmt.functionName);
                isValidated = false;
            }else{
                for(int i = 0; i < paramPassed; ++i){
                    int exprIndex = stmt.argumentExprList.at(i);
                    if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size())){
                        diagnostic.error(tr("Invalid reference"),
                                         tr("Expression index (%1) of call statement is invalid").arg(
                                             QString::number(exprIndex)
                                           )
                                         );
                        isValidated = false;
                    }else{
                        QString paramName;
                        ValueType expectedTy = f.getLocalVariableType(i,&paramName);
                        ValueType actualTy = exprList.at(exprIndex)->getExpressionType();
                        if(Q_UNLIKELY(expectedTy != actualTy)){
                            diagnostic.error(tr("Type mismatch"),
                                             tr("Function argument %1 expects type %2 but got %3 from call").arg(
                                                 QString::number(i),
                                                 getTypeNameString(expectedTy),
                                                 getTypeNameString(actualTy)
                                               ),
                                             tr("Parameter \"%1\" of function \"%2\"").arg(paramName, f.getName())
                                             );
                            isValidated = false;
                        }
                    }
                }
            }
        }
    }
    QHash<QString, int> labelNameToStatementIndex;
    Q_ASSERT(labels.size() == labeledStmtIndexList.size());
    for(int i = 0, num = labels.size(); i < num; ++i){
        const QString& name = labels.at(i);
        int stmtIndex = labeledStmtIndexList.at(i);
        auto iter = labelNameToStatementIndex.find(name);
        if(Q_LIKELY(iter == labelNameToStatementIndex.end())){
            labelNameToStatementIndex.insert(name, stmtIndex);
        }else{
            diagnostic.error(tr("Duplicated label"),
                             tr("More than one label have the same name"),
                             name);
            isValidated = false;
        }
    }
    branchStmtList.clear();
    branchStmtList.reserve(branchTempStmtList.size());
    auto resolveLabel = [&](BranchStatementTemp::BranchActionType ty, const QString& labelName, int& stmtIndex)->void{
        switch(ty){
        case BranchStatementTemp::BranchActionType::Unreachable:
            stmtIndex = -2;
            break;
        case BranchStatementTemp::BranchActionType::Fallthrough:
            stmtIndex = -1;
            break;
        case BranchStatementTemp::BranchActionType::Jump:{
            auto iter = labelNameToStatementIndex.find(labelName);
            if(Q_LIKELY(iter != labelNameToStatementIndex.end())){
                stmtIndex = iter.value();
            }else{
                diagnostic.error(tr("Unresolved label reference"),
                                 tr("Branch statement references a label that do not exist"),
                                 labelName);
                isValidated = false;
            }
        }break;
        }
    };
    for(const auto& stmt: branchTempStmtList){
        BranchStatement cooked;
        resolveLabel(stmt.defaultAction, stmt.defaultJumpLabelName, cooked.defaultStmtIndex);
        cooked.cases.reserve(stmt.cases.size());
        for(int i = 0, num = stmt.cases.size(); i < num; ++i){
            const auto& brcase = stmt.cases.at(i);
            BranchStatement::BranchCase c;
            c.exprIndex = brcase.exprIndex;
            resolveLabel(brcase.action, brcase.labelName, c.stmtIndex);
            cooked.cases.push_back(c);

            // we currently expect Int64 or ValuePtr as branch condition
            int exprIndex = c.exprIndex;
            if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size())){
                diagnostic.error(tr("Invalid reference"),
                                 tr("Expression index (%1) of branch case %2 is invalid").arg(
                                     QString::number(exprIndex),
                                     QString::number(i)
                                   )
                                 );
                isValidated = false;
            }else{
                ValueType ty = exprList.at(exprIndex)->getExpressionType();
                if(Q_UNLIKELY(ty != ValueType::Int64 && ty != ValueType::ValuePtr)){
                    diagnostic.error(tr("Type mismatch"),
                                     tr("Expression %1 of type %2 in branch case %3 cannot be used as branch condition").arg(
                                         QString::number(exprIndex),
                                         getTypeNameString(ty),
                                         QString::number(i)
                                       )
                                     );
                    isValidated = false;
                }
            }
        }
        branchStmtList.push_back(cooked);
    }

    if(Q_UNLIKELY(paramCount < 0 || paramCount >= localVariableNames.size())){
        diagnostic.error(tr("Invalid value"),
                         tr("Number of parameter (%1) is invalid").arg(
                             QString::number(paramCount)
                           )
                         );
        isValidated = false;
    }

    if(Q_UNLIKELY(requiredParamCount < 0 || requiredParamCount > paramCount)){
        diagnostic.error(tr("Invalid value"),
                         tr("Number of required parameter (%1) is invalid").arg(
                             QString::number(requiredParamCount)
                           )
                         );
        isValidated = false;
    }else{
        // the rest of parameters should all have default initializer
        for(int i = requiredParamCount+1; i < qMin(paramCount, localVariableInitializer.size()); ++i){
            if(Q_UNLIKELY(!localVariableInitializer.at(i).isValid())){
                diagnostic.error(tr("Missing initializer"),
                                 tr("Optional parameter %1(%2) do not have an initializer").arg(
                                     QString::number(i),
                                     localVariableNames.at(i)));
                isValidated = false;
            }
        }
    }

    localVariableNameToIndex.clear();
    for(int i = 0, num = localVariableNames.size(); i < num; ++i){
        const QString& name = localVariableNames.at(i);
        if(Q_UNLIKELY(!IRNodeType::validateMemberName(diagnostic, name))){
            isValidated = false;
        }else{
            auto iter = localVariableNameToIndex.find(name);
            if(Q_LIKELY(iter == localVariableNameToIndex.end())){
                localVariableNameToIndex.insert(name, i);

                // check if the initializer is good
                const QVariant& initializer = localVariableInitializer.at(i);
                if(initializer.isValid()){
                    ValueType initTy = getValueType(static_cast<QMetaType::Type>(initializer.userType()));
                    ValueType expectedTy = localVariableTypes.at(i);
                    if(Q_UNLIKELY(initTy != expectedTy)){
                        diagnostic.error(tr("Type mismatch"),
                                         tr("Local variable is in type %1 but initializer is in type %2").arg(
                                             getTypeNameString(expectedTy),
                                             getTypeNameString(initTy)));
                        isValidated = false;
                    }
                }
            }else{
                diagnostic.error(tr("Name conflict"),
                                 tr("More than one local variable with the same name"),
                                 name);
                isValidated = false;
            }
        }
    }

    return isValidated;
}

Task::Task(const IRRootType& root)
    : root(root)
{
    Q_ASSERT(root.validated());
    int num = root.getNumNodeType();

    // dummy ones
    NodeCallbackRecord record = {-1,-1};
    MemberDecl decl;

    nodeCallbacks.push_back(QList<NodeCallbackRecord>());
    nodeCallbacks.back().reserve(num);
    nodeMemberDecl.reserve(num);
    for(int i = 0; i < num; ++i){
        nodeCallbacks.back().push_back(record);
        nodeMemberDecl.push_back(decl);
    }
}

int Task::addNewPass()
{
    int passIndex = nodeCallbacks.size();
    NodeCallbackRecord record = {-1,-1};
    nodeCallbacks.push_back(QList<NodeCallbackRecord>());
    auto& ref = nodeCallbacks.back();
    for(int i = 0, num = root.getNumNodeType(); i < num; ++i){
        ref.push_back(record);
    }
    return passIndex;
}

bool Task::validate(DiagnosticEmitterBase& diagnostic)
{
    isValidated = true;
    auto checkDomain = [&](MemberDecl& decl)->void{
        decl.varNameToIndex.clear();
        for(int i = 0, len = decl.varNameList.size(); i < len; ++i){
            const QString& str = decl.varNameList.at(i);
            if(Q_UNLIKELY(!IRNodeType::validateMemberName(diagnostic, str))){
                isValidated = false;
                continue;
            }
            auto iter = decl.varNameToIndex.find(str);
            if(Q_LIKELY(iter == decl.varNameToIndex.end())){
                decl.varNameToIndex.insert(str, i);

                // also check initializer type error
                const QVariant& initializer = decl.varInitializerList.at(i);
                if(initializer.isValid()){
                    ValueType ty = decl.varTyList.at(i);
                    QMetaType::Type mty = static_cast<QMetaType::Type>(initializer.userType());
                    if(Q_UNLIKELY(mty != getQMetaType(ty))){
                        diagnostic.error(tr("Type mismatch"),
                                         tr("variable is in type %1 but the initializer is in type %2").arg(
                                             getTypeNameString(ty),
                                             getTypeNameString(getValueType(mty))
                                           ),
                                         str);
                        isValidated = false;
                    }
                }
            }else{
                diagnostic.error(tr("Name conflict"),
                                 tr("More than one variable with the same name"),
                                 str);
                isValidated = false;
            }
        }
    };
    // check if there is a name clash among global variables
    diagnostic.pushNode(tr("Global Variable"));
    checkDomain(globalVariables);
    diagnostic.popNode();

    // check if there is a name clash among functions
    functionNameToIndex.clear();
    diagnostic.pushNode(tr("Function"));
    for(int i = 0, len = functions.size(); i < len; ++i){
        const QString& name = functions.at(i).getName();
        if(!IRNodeType::validateMemberName(diagnostic, name)){
            isValidated = false;
            continue;
        }
        auto iter = functionNameToIndex.find(name);
        if(Q_LIKELY(iter == functionNameToIndex.end())){
            functionNameToIndex.insert(name, i);
        }else{
            diagnostic.error(tr("Name conflict"),
                             tr("More than one function with the same name"),
                             name);
            isValidated = false;
        }
    }
    diagnostic.popNode();

    QQueue<int> reachableFunctions;
    RunTimeSizeArray<bool> functionReachable(static_cast<std::size_t>(functions.size()), false);
    auto tryEnqueue = [&](int index)->void{
        if(!functionReachable.at(index)){
            reachableFunctions.enqueue(index);
            functionReachable.at(index) = true;
        }
    };
    // check if any callback reference is invalid
    bool isAnyCallbackSet = false;
    diagnostic.pushNode(tr("Callback"));
    for(int passIndex = 0, numPass = nodeCallbacks.size(); passIndex < numPass; ++passIndex){
        const auto& list = nodeCallbacks.at(passIndex);
        diagnostic.pushNode(tr("Pass %1").arg(passIndex));
        for(int i = 0, len = root.getNumNodeType(); i < len; ++i){
            const auto& cbs = list.at(i);
            if(cbs.onEntryFunctionIndex >= 0){
                if(Q_UNLIKELY(cbs.onEntryFunctionIndex >= functions.size())){
                    diagnostic.error(tr("Invalid reference"),
                                     tr("on entry callback (%1) for node type %2 is invalid").arg(
                                         QString::number(cbs.onEntryFunctionIndex),
                                         QString::number(i)
                                       ),
                                     root.getNodeType(i).getName());
                    isValidated = false;
                }else{
                    tryEnqueue(cbs.onEntryFunctionIndex);
                    isAnyCallbackSet = true;
                }
            }
            if(cbs.onExitFunctionIndex >= 0){
                if(Q_UNLIKELY(cbs.onExitFunctionIndex >= functions.size())){
                    diagnostic.error(tr("Invalid reference"),
                                     tr("on exit callback (%1) for node type %2 is invalid").arg(
                                         QString::number(cbs.onExitFunctionIndex),
                                         QString::number(i)
                                       ),
                                     root.getNodeType(i).getName());
                    isValidated = false;
                }else{
                    tryEnqueue(cbs.onExitFunctionIndex);
                    isAnyCallbackSet = true;
                }
            }
        }
        diagnostic.popNode();
    }
    diagnostic.popNode();

    if(Q_UNLIKELY(!isAnyCallbackSet)){
        diagnostic.error(tr("Empty task"),
                         tr("No callback is specified for the task"));
        isValidated = false;
    }

    if(Q_UNLIKELY(!isValidated))
        return isValidated;

    for(int i = 0, len = functions.size(); i < len; ++i){
        diagnostic.pushNode(tr("Function[%1] %2").arg(QString::number(i)));
        // no short circuit
        isValidated = functions[i].validate(diagnostic, *this) && isValidated;
        diagnostic.popNode();
    }

    // check if any function is unreachable (warning only)
    while(!reachableFunctions.empty()){
        int index = reachableFunctions.dequeue();
        const Function& f = functions.at(index);
        const auto& list = f.getReferencedFunctions();
        for(const auto& str : list){
            int index = getFunctionIndex(str);
            tryEnqueue(index);
        }
    }

    for(int i = 0, len = functions.size(); i < len; ++i){
        if(!functionReachable.at(i)){
            diagnostic.warning(tr("Unreachable function"),
                               tr("Function %1 is not used").arg(
                                   QString::number(i)
                                 ),
                               functions.at(i).getName()
                               );
            // just a warning; no invalidation
        }
    }
    return isValidated;
}
