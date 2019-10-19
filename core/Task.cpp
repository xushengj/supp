#include "core/Task.h"

#include "core/DiagnosticEmitter.h"
#include "core/Expression.h"
#include "core/IR.h"
#include "util/ADT.h"

#include <QQueue>

#include <functional>

bool Function::validate(DiagnosticEmitterBase& diagnostic, const Task& task)
{
    // POSSIBLE IMPROVEMENT:
    // 1. find unreachable expression
    // 2. find unreachable statement (maybe just statically unreachable statements)
    bool isValidated = true;

    // check if name is good
    if(Q_LIKELY(IRNodeType::validateName(diagnostic, functionName))){
        diagnostic.attachDescriptiveName(functionName);
    }else{
        isValidated = false;
    }

    // check if extern variable reference is good
    // (i.e. no name conflict)
    // for now we do not attempt to check if the name resolve to given type, because it requires runtime data
    Q_ASSERT(externVariableNameList.size() == externVariableTypeList.size());
    for(int i = 0, num = externVariableNameList.size(); i < num; ++i){
        // check if searching for this name result in the correct index
        const QString& varName = externVariableNameList.at(i);
        int searchResult = externVariableNameToIndex.value(varName, -1);
        if(Q_UNLIKELY(searchResult != i)){
            diagnostic(Diag::Error_Func_NameClash_ExternVariable, varName, searchResult, i);
            isValidated = false;
        }
        // no void extern reference
        ValueType expectedTy = externVariableTypeList.at(i);
        if(Q_UNLIKELY(expectedTy == ValueType::Void)){
            diagnostic(Diag::Error_Func_BadType_ExternVariableVoid, varName);
            isValidated = false;
        }
    }
    if(isValidated){
        Q_ASSERT(externVariableNameToIndex.size() == externVariableNameList.size());
    }

    // check if local variables are good
    if(Q_UNLIKELY(paramCount < 0 || paramCount > localVariableNames.size())){
        diagnostic(Diag::Error_Func_InvalidValue_TotalParamCount, paramCount);
        isValidated = false;
    }

    if(Q_UNLIKELY(requiredParamCount < 0 || requiredParamCount > paramCount)){
        diagnostic(Diag::Error_Func_InvalidValue_RequiredParamCount, requiredParamCount);
        isValidated = false;
    }else{
        // the rest of parameters should all have default initializer
        for(int i = requiredParamCount+1; i < qMin(paramCount, localVariableInitializer.size()); ++i){
            if(Q_UNLIKELY(!localVariableInitializer.at(i).isValid())){
                diagnostic(Diag::Error_Func_MissingInitializer_OptionalParam, i, localVariableNames.at(i));
                isValidated = false;
            }
        }
    }

    localVariableNameToIndex.clear();
    for(int i = 0, num = localVariableNames.size(); i < num; ++i){
        const QString& name = localVariableNames.at(i);
        if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, name))){
            isValidated = false;
        }else{
            auto iter = localVariableNameToIndex.find(name);
            if(Q_LIKELY(iter == localVariableNameToIndex.end())){
                localVariableNameToIndex.insert(name, i);

                // no void local variables
                ValueType expectedTy = localVariableTypes.at(i);
                if(Q_UNLIKELY(expectedTy == ValueType::Void)){
                    diagnostic(Diag::Error_Func_BadType_LocalVariableVoid, name);
                    isValidated = false;
                }

                // check if the initializer is good
                const QVariant& initializer = localVariableInitializer.at(i);
                if(initializer.isValid()){
                    ValueType initTy = getValueType(static_cast<QMetaType::Type>(initializer.userType()));
                    if(Q_UNLIKELY(initTy != expectedTy)){
                        diagnostic(Diag::Error_Func_BadInitializer_LocalVariable, i, name, expectedTy, initTy);
                        isValidated = false;
                    }
                }
            }else{
                diagnostic(Diag::Error_Func_NameClash_LocalVariable, name, iter.value(), i);
                isValidated = false;
            }
        }
    }

    // check if all expressions are good
    // 1. no circular dependence (just check if any expression is referencing another expression with larger index)
    // 2. type expectation should match
    // 3. if the expression need to reference variable by name, the name should either appear in local variable or in extern variable list
    for(int index = 0, len = exprList.size(); index < len; ++index){
        const ExpressionBase* ptr = exprList.at(index);
        QList<int> dependentExprIndices;
        QList<ValueType> dependentExprTypes;
        ptr->getDependency(dependentExprIndices, dependentExprTypes);
        Q_ASSERT(dependentExprIndices.size() == dependentExprTypes.size());
        for(int i = 0, len = dependentExprIndices.size(); i < len; ++i){
            int exprIndex = dependentExprIndices.at(i);
            if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size() || exprIndex >= index)){
                diagnostic(Diag::Error_Func_BadExprDependence_BadIndex, index, exprIndex);
                isValidated = false;
            }else{
                if(Q_UNLIKELY(ptr->getExpressionType() != dependentExprTypes.at(i))){
                    diagnostic(Diag::Error_Func_BadExprDependence_TypeMismatch,
                               index, exprIndex, dependentExprTypes.at(i), ptr->getExpressionType());
                    isValidated = false;
                }
            }
        }
        QList<QString> nameReference;
        ptr->getVariableNameReference(nameReference);
        if(!nameReference.empty()){
            for(const auto& name : nameReference){
                if(localVariableNameToIndex.value(name, -1) == -1){
                    if(Q_UNLIKELY(externVariableNameToIndex.value(name, -1) == -1)){
                        diagnostic(Diag::Error_Func_BadExpr_BadNameReference, index, name);
                        isValidated = false;
                    }
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
            if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, stmt.lvalueName))){
                isValidated = false;
            }
        }else{
            // lhs should be valueptr
            if(Q_UNLIKELY(stmt.lvalueExprIndex < 0 || stmt.lvalueExprIndex >= exprList.size())){
                diagnostic(Diag::Error_Func_Stmt_BadExprIndex, stmt.lvalueExprIndex);
                isValidated = false;
            }else if(Q_UNLIKELY(exprList.at(stmt.lvalueExprIndex)->getExpressionType() != ValueType::ValuePtr)){
                diagnostic(Diag::Error_Func_Assign_BadLHS_Type, stmt.lvalueExprIndex, exprList.at(stmt.lvalueExprIndex)->getExpressionType());
                isValidated = false;
            }
        }
        if(Q_UNLIKELY(stmt.rvalueExprIndex < 0 || stmt.rvalueExprIndex >= exprList.size())){
            diagnostic(Diag::Error_Func_Stmt_BadExprIndex, stmt.rvalueExprIndex);
            isValidated = false;
        }else{
            ValueType ty = exprList.at(stmt.rvalueExprIndex)->getExpressionType();
            if(Q_UNLIKELY(ty == ValueType::Void)){
                diagnostic(Diag::Error_Func_Assign_BadRHS_RHSVoid, stmt.rvalueExprIndex);
                isValidated = false;
            }else if(stmt.lvalueExprIndex == -1){
                // we only test rhs type match if the assignment is by name
                ValueType expectedTy = ValueType::Void;

                int localVarIndex = localVariableNameToIndex.value(stmt.lvalueName, -1);
                if(localVarIndex < 0){
                    int externVarIndex = externVariableNameToIndex.value(stmt.lvalueName, -1);
                    if(Q_UNLIKELY(externVarIndex < 0)){
                        diagnostic(Diag::Error_Func_Assign_BadLHS_BadNameReference, stmt.lvalueName);
                        isValidated = false;
                    }else{
                        expectedTy = externVariableTypeList.at(externVarIndex);
                    }
                }else{
                    expectedTy = localVariableTypes.at(localVarIndex);
                }

                if(Q_UNLIKELY(ty != expectedTy)){
                    diagnostic(Diag::Error_Func_Assign_BadRHS_VariableTypeMismatch,
                               stmt.lvalueName, expectedTy, stmt.rvalueExprIndex, ty);
                    isValidated = false;
                }
            }
        }
    }
    for(const auto& stmt: outputStmtList){
        // for now we only output text, so output statement should only accept string expression
        // later on we may check output type based on configuration from Task or ExecutionContext
        int exprIndex = stmt.exprIndex;
        if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size())){
            diagnostic(Diag::Error_Func_Stmt_BadExprIndex, exprIndex);
            isValidated = false;
        }else{
            const ExpressionBase* ptr = exprList.at(exprIndex);
            if(Q_UNLIKELY(ptr->getExpressionType() != ValueType::String)){
                diagnostic(Diag::Error_Func_Output_BadRHS_Type, exprIndex, ptr->getExpressionType());
                isValidated = false;
            }
        }
    }
    calledFunctions.clear();
    QHash<QString, int> calledFunctionNameToIndex; // actually just use it as a set
    for(const auto& stmt: callStmtList){
        int functionIndex = task.getFunctionIndex(stmt.functionName);
        if(Q_UNLIKELY(functionIndex == -1)){
            diagnostic(Diag::Error_Func_Call_CalleeNotFound, stmt.functionName);
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
            if(Q_UNLIKELY(paramPassed < requiredParamCount || paramPassed > paramCount)){
                diagnostic(Diag::Error_Func_Call_BadParamList_Count, stmt.functionName, paramCount, requiredParamCount, paramPassed);
                isValidated = false;
            }else{
                for(int i = 0; i < paramPassed; ++i){
                    int exprIndex = stmt.argumentExprList.at(i);
                    if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size())){
                        diagnostic(Diag::Error_Func_Stmt_BadExprIndex, exprIndex);
                        isValidated = false;
                    }else{
                        QString paramName = f.getLocalVariableName(i);
                        ValueType expectedTy = f.getLocalVariableType(i);
                        ValueType actualTy = exprList.at(exprIndex)->getExpressionType();
                        if(Q_UNLIKELY(expectedTy != actualTy)){
                            diagnostic(Diag::Error_Func_Call_BadParamList_Type,
                                       stmt.functionName, i, paramName, expectedTy, actualTy);
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
            diagnostic(Diag::Error_Func_DuplicateLabel, name, iter.value(), stmtIndex);
            isValidated = false;
        }
    }
    branchStmtList.clear();
    branchStmtList.reserve(branchTempStmtList.size());
    auto resolveLabel = [&](BranchStatementTemp::BranchActionType ty, int caseIndex, const QString& labelName, int& stmtIndex)->void{
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
                diagnostic(Diag::Error_Func_Branch_BadLabelReference, labelName, caseIndex);
                isValidated = false;
            }
        }break;
        }
    };
    for(const auto& stmt: branchTempStmtList){
        BranchStatement cooked;
        resolveLabel(stmt.defaultAction, -1, stmt.defaultJumpLabelName, cooked.defaultStmtIndex);
        cooked.cases.reserve(stmt.cases.size());
        for(int i = 0, num = stmt.cases.size(); i < num; ++i){
            const auto& brcase = stmt.cases.at(i);
            BranchStatement::BranchCase c;
            c.exprIndex = brcase.exprIndex;
            resolveLabel(brcase.action, i, brcase.labelName, c.stmtIndex);
            cooked.cases.push_back(c);

            // we currently expect Int64 or ValuePtr as branch condition
            int exprIndex = c.exprIndex;
            if(Q_UNLIKELY(exprIndex < 0 || exprIndex >= exprList.size())){
                diagnostic(Diag::Error_Func_Stmt_BadExprIndex_BranchCondition, exprIndex, i);
                isValidated = false;
            }else{
                ValueType ty = exprList.at(exprIndex)->getExpressionType();
                if(Q_UNLIKELY(ty != ValueType::Int64 && ty != ValueType::ValuePtr)){
                    diagnostic(Diag::Error_Func_Branch_BadConditionType, i, exprIndex, ty);
                    isValidated = false;
                }
            }
        }
        branchStmtList.push_back(cooked);
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
            if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, str))){
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
                    ValueType initializerTy = getValueType(static_cast<QMetaType::Type>(initializer.userType()));
                    if(Q_UNLIKELY(initializerTy != ty)){
                        diagnostic(Diag::Error_Task_BadInitializer_ExternVariable, str, ty, initializerTy);
                        isValidated = false;
                    }
                }
            }else{
                diagnostic(Diag::Error_Task_NameClash_ExternVariable, str, iter.value(), i);
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
        if(!IRNodeType::validateName(diagnostic, name)){
            isValidated = false;
            continue;
        }
        auto iter = functionNameToIndex.find(name);
        if(Q_LIKELY(iter == functionNameToIndex.end())){
            functionNameToIndex.insert(name, i);
        }else{
            diagnostic(Diag::Error_Task_NameClash_Function, name, iter.value(), i);
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
                    diagnostic(Diag::Error_Task_BadFunctionIndex_NodeTraverseCallback,
                               root.getNodeType(i).getName(), passIndex, cbs.onEntryFunctionIndex);
                    isValidated = false;
                }else{
                    tryEnqueue(cbs.onEntryFunctionIndex);
                    isAnyCallbackSet = true;
                }
            }
            if(cbs.onExitFunctionIndex >= 0){
                if(Q_UNLIKELY(cbs.onExitFunctionIndex >= functions.size())){
                    diagnostic(Diag::Error_Task_BadFunctionIndex_NodeTraverseCallback,
                               root.getNodeType(i).getName(), passIndex, cbs.onExitFunctionIndex);
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
        diagnostic(Diag::Error_Task_NoCallback);
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
        const auto& list = f.getReferencedFunctionList();
        for(const auto& str : list){
            int index = getFunctionIndex(str);
            tryEnqueue(index);
        }
    }

    for(int i = 0, len = functions.size(); i < len; ++i){
        if(!functionReachable.at(i)){
            diagnostic(Diag::Warn_Task_UnreachableFunction, functions.at(i).getName());
        }
    }
    return isValidated;
}
