#include "core/ExecutionContext.h"

#include "core/DiagnosticEmitter.h"
#include "core/Expression.h"
#include "core/IR.h"
#include "core/OutputHandlerBase.h"
#include "core/Task.h"

#include <stdexcept>

ExecutionContext::ExecutionContext(const Task& t, const IRRootInstance &root, DiagnosticEmitterBase& diagnostic, OutputHandlerBase& out, QObject* parent)
    : QObject(parent),
      t(t),
      root(root),
      diagnostic(diagnostic),
      out(out)
{
    Q_ASSERT(t.validated());
    Q_ASSERT(root.validated());

    // initialize global variables
    int gvCnt = t.getNumGlobalVariable();
    globalVariables.clear();
    globalVariables.reserve(gvCnt);
    for(int i = 0; i < gvCnt; ++i){
        globalVariables.push_back(t.getGlobalVariableInitializer(i));
    }

    // initialize node variables
    //const auto& rootTy = t.getRootType();
    int nodeCount = root.getNumNode();
    nodeMembers.clear();
    nodeMembers.reserve(nodeCount);
    QHash<int, QList<QVariant>> initializerListTemplate;//[nodeTypeIndex] -> combined member initialization list
    for(int i = 0; i < nodeCount; ++i){
        int typeIndex = root.getNode(i).getTypeIndex();
        auto iter = initializerListTemplate.find(typeIndex);
        if(iter == initializerListTemplate.end()){
            QList<QVariant> initializerList;
            int nodeMemberCount = t.getNumNodeMember(typeIndex);
            initializerList.reserve(nodeMemberCount);
            for(int i = 0; i < nodeMemberCount; ++i){
                initializerList.push_back(t.getNodeMemberInitializer(typeIndex, i));
            }
            initializerListTemplate.insert(typeIndex, initializerList);
            nodeMembers.push_back(initializerList);
        }else{
            nodeMembers.push_back(iter.value());
        }
    }
    out.getAllowedOutputTypeList(allowedOutputTypes);
    currentActivationCount = 0;
}

bool ExecutionContext::read(const QString& name, ValueType& ty, QVariant& val)
{
    Q_ASSERT(!stack.empty());
    const auto& frame = stack.top();

    {
        int localVariableIndex = frame.f.getLocalVariableIndex(name);
        if(localVariableIndex >= 0){
            ty = frame.f.getLocalVariableType(localVariableIndex);
            val = frame.localVariables.at(localVariableIndex);
            checkUninitializedRead(ty, val);
            return true;
        }
    }

    {
        int nodeMemberIndex = t.getNodeMemberIndex(frame.irNodeTypeIndex, name);
        if(nodeMemberIndex >= 0){
            ty = t.getNodeMemberType(frame.irNodeTypeIndex, nodeMemberIndex);
            val = nodeMembers.at(frame.irNodeIndex).at(nodeMemberIndex);
            checkUninitializedRead(ty, val);
            return true;
        }
    }

    {
        const auto& nodeTy = root.getType().getNodeType(frame.irNodeTypeIndex);
        int nodeParameterIndex = nodeTy.getParameterIndex(name);
        if(nodeParameterIndex >= 0){
            const auto& nodeInst = root.getNode(frame.irNodeIndex);
            ty = nodeTy.getParameterType(nodeParameterIndex);
            val = nodeInst.getParameter(nodeParameterIndex);
            checkUninitializedRead(ty, val);
            return true;
        }
    }

    {
        int globalVariableIndex = t.getGlobalVariableIndex(name);
        if(globalVariableIndex >= 0){
            ty = t.getGlobalVariableType(globalVariableIndex);
            val = globalVariables.at(globalVariableIndex);
            checkUninitializedRead(ty, val);
            return true;
        }
    }

    diagnostic(Diag::Error_Exec_BadReference_VariableRead, name);
    return false;
}

bool ExecutionContext::read(const ValuePtrType& valuePtr, ValueType& ty, QVariant& val)
{
    Q_ASSERT(!stack.empty());
    const auto& frame = stack.top();// index is stack.size()-1

    switch(valuePtr.ty){
    case ValuePtrType::PtrType::NullPointer:{
        diagnostic(Diag::Error_Exec_NullPointerException_ReadValue, getValuePtrDescription(valuePtr));
        return false;
    }/*break;*/
    case ValuePtrType::PtrType::LocalVariable:{
        int ptrAcivationIndex = valuePtr.head.activationIndex;
        if(frame.activationIndex != ptrAcivationIndex){
            // walk over the stack to see if the activation is still alive
            for(int i = static_cast<int>(stack.size())-2; i >= 0; ++i){
                const auto& curFrame = stack.at(i);
                if(curFrame.activationIndex == ptrAcivationIndex){
                    // okay we found it
                    ty = curFrame.f.getLocalVariableType(valuePtr.valueIndex);
                    val = curFrame.localVariables.at(valuePtr.valueIndex);
                    checkUninitializedRead(ty, val);
                    return true;
                }
            }
            diagnostic(Diag::Error_Exec_DanglingPointerException_ReadValue, getValuePtrDescription(valuePtr));
            return false;
        }else{
            ty = frame.f.getLocalVariableType(valuePtr.valueIndex);
            val = frame.localVariables.at(valuePtr.valueIndex);
            checkUninitializedRead(ty, val);
            return true;
        }
    }/*break;*/
    case ValuePtrType::PtrType::NodeRWMember:{
        ty = t.getNodeMemberType(
                    /* node type index */root.getNode(valuePtr.nodeIndex).getTypeIndex(),
                    /* member index    */valuePtr.valueIndex);
        val = nodeMembers.at(valuePtr.nodeIndex).at(valuePtr.valueIndex);
        checkUninitializedRead(ty, val);
        return true;
    }/*break;*/
    case ValuePtrType::PtrType::NodeROParameter:{
        const auto& nodeTy = root.getType().getNodeType(valuePtr.nodeIndex);
        ty = nodeTy.getParameterType(valuePtr.valueIndex);
        val = root.getNode(valuePtr.nodeIndex).getParameter(valuePtr.valueIndex);
        checkUninitializedRead(ty, val);
        return true;
    }/*break;*/
    case ValuePtrType::PtrType::GlobalVariable:{
        ty = t.getGlobalVariableType(valuePtr.valueIndex);
        val = globalVariables.at(valuePtr.valueIndex);
        checkUninitializedRead(ty, val);
        return true;
    }/*break;*/
    }
    Q_UNREACHABLE();
}

void ExecutionContext::checkUninitializedRead(ValueType ty, QVariant& readVal)
{
    if(readVal.isValid())
        return;
    diagnostic(Diag::Warn_Exec_UninitializedRead);

    //default initialize it
    switch (ty) {
    case ValueType::Void: Q_UNREACHABLE();
    case ValueType::Int64:
        readVal = qint64(0);
        break;
    case ValueType::String:
        readVal = QString();
        break;
    case ValueType::NodePtr:{
        NodePtrType ptr;
        ptr.head = getPtrSrcHead();
        ptr.nodeIndex = -1;
        readVal.setValue(ptr);
    }break;
    case ValueType::ValuePtr:{
        ValuePtrType ptr;
        ptr.head = getPtrSrcHead();
        ptr.ty = ValuePtrType::PtrType::NullPointer;
        ptr.nodeIndex = -1;
        ptr.valueIndex = -1;
        readVal.setValue(ptr);
    }break;
    }
}

bool ExecutionContext::takeAddress(const QString& name, ValuePtrType& val)
{
    Q_ASSERT(!stack.empty());
    const auto& frame = stack.top();

    val.head = getPtrSrcHead();
    {
        int localVariableIndex = frame.f.getLocalVariableIndex(name);
        if(localVariableIndex >= 0){
            val.ty = ValuePtrType::PtrType::LocalVariable;
            val.nodeIndex = -1;
            val.valueIndex = localVariableIndex;
            return true;
        }
    }

    {
        int nodeMemberIndex = t.getNodeMemberIndex(frame.irNodeTypeIndex, name);
        if(nodeMemberIndex >= 0){
            val.ty = ValuePtrType::PtrType::NodeRWMember;
            val.nodeIndex = frame.irNodeIndex;
            val.valueIndex = nodeMemberIndex;
            return true;
        }
    }

    {
        const auto& nodeTy = root.getType().getNodeType(frame.irNodeTypeIndex);
        int nodeParameterIndex = nodeTy.getParameterIndex(name);
        if(nodeParameterIndex >= 0){
            val.ty = ValuePtrType::PtrType::NodeROParameter;
            val.nodeIndex = frame.irNodeIndex;
            val.valueIndex = nodeParameterIndex;
            return true;
        }
    }

    {
        int globalVariableIndex = t.getGlobalVariableIndex(name);
        if(globalVariableIndex >= 0){
            val.ty = ValuePtrType::PtrType::GlobalVariable;
            val.nodeIndex = -1;
            val.valueIndex = globalVariableIndex;
            return true;
        }
    }

    diagnostic(Diag::Error_Exec_BadReference_VariableTakeAddress, name);
    return false;
}

bool ExecutionContext::write(const QString& name, const ValueType& ty, const QVariant& val)
{
    Q_ASSERT(!stack.empty());
    auto& frame = stack.top();
    ValueType actualTy = ValueType::Void;
    QVariant* valPtr = nullptr;

    {
        int localVariableIndex = frame.f.getLocalVariableIndex(name);
        if(localVariableIndex >= 0){
            actualTy = frame.f.getLocalVariableType(localVariableIndex);
            valPtr = &frame.localVariables[localVariableIndex];
        }
    }

    if(!valPtr){
        int nodeMemberIndex = t.getNodeMemberIndex(frame.irNodeTypeIndex, name);
        if(nodeMemberIndex >= 0){
            actualTy = t.getNodeMemberType(frame.irNodeTypeIndex, nodeMemberIndex);
            valPtr = &nodeMembers[frame.irNodeIndex][nodeMemberIndex];
        }
    }

    // we block write to read-only node parameters
    if(!valPtr){
        const auto& nodeTy = root.getType().getNodeType(frame.irNodeTypeIndex);
        int nodeParameterIndex = nodeTy.getParameterIndex(name);
        if(Q_UNLIKELY(nodeParameterIndex >= 0)){
            diagnostic(Diag::Error_Exec_WriteToConst_WriteNodeParamByName, name);
            return false;
        }
    }

    if(!valPtr){
        int globalVariableIndex = t.getGlobalVariableIndex(name);
        if(globalVariableIndex >= 0){
            actualTy = t.getGlobalVariableType(globalVariableIndex);
            valPtr = &globalVariables[globalVariableIndex];
        }
    }

    if(Q_UNLIKELY(!valPtr)){
        diagnostic(Diag::Error_Exec_BadReference_VariableWrite, name);
        return false;
    }else if(Q_UNLIKELY(actualTy != ty)){
        diagnostic(Diag::Error_Exec_TypeMismatch_WriteByName, ty, actualTy, name);
        return false;
    }else{
        *valPtr = val;
        return true;
    }
}

bool ExecutionContext::write(const ValuePtrType& valuePtr, const ValueType& ty, const QVariant& dest)
{
    Q_ASSERT(!stack.empty());
    auto& frame = stack.top();// index is stack.size()-1
    ValueType actualTy = ValueType::Void;
    QVariant* valPtr = nullptr;

    switch(valuePtr.ty){
    case ValuePtrType::PtrType::NullPointer:{
        diagnostic(Diag::Error_Exec_NullPointerException_WriteValue, getValuePtrDescription(valuePtr));
        return false;
    }/*break;*/
    case ValuePtrType::PtrType::LocalVariable:{
        int ptrAcivationIndex = valuePtr.head.activationIndex;
        if(frame.activationIndex != ptrAcivationIndex){
            // walk over the stack to see if the activation is still alive
            for(int i = static_cast<int>(stack.size())-2; i >= 0; ++i){
                auto& curFrame = stack.at(i);
                if(curFrame.activationIndex == ptrAcivationIndex){
                    // okay we found it
                    actualTy = curFrame.f.getLocalVariableType(valuePtr.valueIndex);
                    valPtr = &curFrame.localVariables[valuePtr.valueIndex];
                    break;
                }
            }
            diagnostic(Diag::Error_Exec_DanglingPointerException_WriteValue, getValuePtrDescription(valuePtr));
            return false;
        }else{
            actualTy = frame.f.getLocalVariableType(valuePtr.valueIndex);
            valPtr = &frame.localVariables[valuePtr.valueIndex];
        }
    }break;
    case ValuePtrType::PtrType::NodeRWMember:{
        actualTy = t.getNodeMemberType(
                    /* node type index */root.getNode(valuePtr.nodeIndex).getTypeIndex(),
                    /* member index    */valuePtr.valueIndex);
        valPtr = &nodeMembers[valuePtr.nodeIndex][valuePtr.valueIndex];
    }break;
    case ValuePtrType::PtrType::NodeROParameter:{
        diagnostic(Diag::Error_Exec_WriteToConst_WriteNodeParamByPointer, getValuePtrDescription(valuePtr));
        return false;
    }/*break;*/
    case ValuePtrType::PtrType::GlobalVariable:{
        actualTy = t.getGlobalVariableType(valuePtr.valueIndex);
        valPtr = &globalVariables[valuePtr.valueIndex];
    }break;
    }

    if(Q_UNLIKELY(actualTy != ty)){
        diagnostic(Diag::Error_Exec_TypeMismatch_WriteByPointer, ty, actualTy, getValuePtrDescription(valuePtr));
        return false;
    }else{
        *valPtr = dest;
        return true;
    }
}

bool ExecutionContext::getCurrentNodePtr(NodePtrType& result)
{
    Q_ASSERT(!stack.empty());
    result.head = getPtrSrcHead();
    result.nodeIndex = stack.top().irNodeIndex;
    return true;
}

bool ExecutionContext::getRootNodePtr(NodePtrType& result)
{
    Q_ASSERT(!stack.empty());
    result.head = getPtrSrcHead();
    result.nodeIndex = 0;
    return true;
}
bool ExecutionContext::getParentNode(const NodePtrType& src, NodePtrType& result)
{
    Q_ASSERT(!stack.empty());

    if(Q_UNLIKELY(src.nodeIndex < 0)){
        diagnostic(Diag::Error_Exec_BadNodePointer_TraverseToParent, getPointerSrcDescription(src.head));
        return false;
    }

    result.head = getPtrSrcHead();
    result.nodeIndex = root.getNode(src.nodeIndex).getParentIndex();
    return true;
}

bool ExecutionContext::getChildNode(const NodePtrType& src, const QString& childName, NodePtrType& result, ValueType keyTy, const QVariant& primaryKey)
{
    if(Q_UNLIKELY(src.nodeIndex < 0)){
        diagnostic(Diag::Error_Exec_BadNodePointer_TraverseToChild, getPointerSrcDescription(src.head));
        return false;
    }

    int childTyIndex = root.getType().getNodeTypeIndex(childName);
    const IRNodeType& childTy = root.getType().getNodeType(childTyIndex);
    int primaryKeyIndex = childTy.getPrimaryKeyParameterIndex();
    if(Q_UNLIKELY(primaryKeyIndex < 0)){
        diagnostic(Diag::Error_Exec_BadTraverse_ChildWithoutPrimaryKey, childName, getNodePtrDescription(src));
        return false;
    }
    if(Q_UNLIKELY(childTy.getParameterType(primaryKeyIndex) != keyTy)){
        diagnostic(Diag::Error_Exec_BadTraverse_PrimaryKeyTypeMismatch,
                   keyTy,
                   childTy.getParameterType(primaryKeyIndex),
                   childTy.getName(),
                   childTy.getParameterName(primaryKeyIndex),
                   getNodePtrDescription(src));
        return false;
    }
    const IRNodeInstance& inst = root.getNode(src.nodeIndex);
    int childTyLocalIndex = inst.getLocalTypeIndex(childTyIndex);
    int childIndex = inst.getChildNodeIndex(childTyLocalIndex, primaryKeyIndex, primaryKey);

    result.head = getPtrSrcHead();
    result.nodeIndex = childIndex;
    return true;
}

bool ExecutionContext::getChildNode(const NodePtrType& src, const QString& childName, NodePtrType& result, const QString& keyField, ValueType keyTy, const QVariant& keyValue)
{
    if(Q_UNLIKELY(src.nodeIndex < 0)){
        diagnostic(Diag::Error_Exec_BadNodePointer_TraverseToChild, getPointerSrcDescription(src.head));
        return false;
    }

    int childTyIndex = root.getType().getNodeTypeIndex(childName);
    const IRNodeType& childTy = root.getType().getNodeType(childTyIndex);
    int paramIndex = childTy.getParameterIndex(keyField);
    if(Q_UNLIKELY(paramIndex < 0)){
        diagnostic(Diag::Error_Exec_BadTraverse_ParameterNotFound,
                   childName,
                   keyField,
                   getNodePtrDescription(src));
        return false;
    }
    if(Q_UNLIKELY(!childTy.getParameterIsUnique(paramIndex))){
        diagnostic(Diag::Error_Exec_BadTraverse_ParameterNotUnique,
                   childName,
                   keyField,
                   getNodePtrDescription(src));
        return false;
    }
    if(Q_UNLIKELY(childTy.getParameterType(paramIndex) != keyTy)){
        diagnostic(Diag::Error_Exec_BadTraverse_UniqueKeyTypeMismatch,
                   keyTy,
                   childTy.getParameterType(paramIndex),
                   childName,
                   keyField,
                   getNodePtrDescription(src));
        return false;
    }
    const IRNodeInstance& inst = root.getNode(src.nodeIndex);
    int childTyLocalIndex = inst.getLocalTypeIndex(childTyIndex);
    int childIndex = inst.getChildNodeIndex(childTyLocalIndex, paramIndex, keyValue);

    result.head = getPtrSrcHead();
    result.nodeIndex = childIndex;
    return true;
}

void ExecutionContext::continueExecution()
{
    // we havn't implement pausing execution yet
    Q_ASSERT(!isInExecution);

    if(!isInExecution){
        try {
            mainExecutionEntry();
            emit executionFinished(0);
        } catch (...) {
            emit executionFinished(-1);
        }
    }else{
        eventLoop.quit();
    }
}

void ExecutionContext::mainExecutionEntry()
{
    // reset state first
    currentActivationCount = 0;
    nodeTraverseStack.clear();
    stack.clear();
    isInExecution = true;

    for(int passIndex = 0, numPass = t.getNumPass(); passIndex < numPass; ++passIndex){
        diagnostic.pushNode(tr("Pass %1").arg(passIndex));
        diagnostic.pushNode(tr("/~"));
        nodeTraverseEntry(passIndex, 0);
        diagnostic.popNode();// "/~"
        diagnostic.popNode();// "Pass %1"
    }
}

void ExecutionContext::nodeTraverseEntry(int passIndex, int nodeIndex)
{
    const IRNodeInstance& inst = root.getNode(nodeIndex);
    int nodeTypeIndex = inst.getTypeIndex();
    const IRNodeType& ty = root.getType().getNodeType(nodeTypeIndex);
    diagnostic.attachDescriptiveName(ty.getName());
    int entryCB = t.getNodeCallback(nodeTypeIndex, Task::CallbackType::OnEntry, passIndex);
    int exitCB = t.getNodeCallback(nodeTypeIndex, Task::CallbackType::OnExit, passIndex);

    if(entryCB >= 0){
        diagnostic.pushNode(tr("|Entry %1"));
        pushFunctionStackframe(entryCB, nodeIndex);
        functionMainLoop();
        diagnostic.popNode();
    }

    for(int i = 0, numChild = inst.getNumChildNode(); i < numChild; ++i){
        diagnostic.pushNode(tr("/[%1]%2").arg(i));
        nodeTraverseEntry(passIndex, inst.getChildNodeByOrder(i));
        diagnostic.popNode();
    }

    if(exitCB >= 0){
        diagnostic.pushNode(tr("|Exit %1"));
        pushFunctionStackframe(exitCB, nodeIndex);
        functionMainLoop();
        diagnostic.popNode();
    }
}

void ExecutionContext::pushFunctionStackframe(int functionIndex, int nodeIndex, QList<QVariant> params)
{
    int activationIndex = currentActivationCount++;

    // do not push the frame if the function has no statements in it
    // (just for performance)
    const Function& f = t.getFunction(functionIndex);
    if(f.getNumStatement() > 0){
        CallStackEntry entry(f, functionIndex, nodeIndex, root.getNode(nodeIndex).getTypeIndex(), activationIndex);
        int localVariableCnt = f.getNumLocalVariable();
        entry.localVariables.reserve(localVariableCnt);
        for(int i = 0; i < localVariableCnt; ++i){
            entry.localVariables.push_back(f.getLocalVariableInitializer(i));
        }
        for(int i = 0, num = params.size(); i < num; ++i){
            entry.localVariables[i] = params.at(i);
        }
        stack.push(entry);
    }
}

void ExecutionContext::functionMainLoop()
{
    while(!stack.empty()){
        auto& frame = stack.top();
        if(frame.stmtIndex >= frame.f.getNumStatement()){
            // implicit return
            Q_ASSERT(frame.stmtIndex == frame.f.getNumStatement());
            stack.pop();
            continue;
        }

        int stmtIndex = frame.stmtIndex;
        const auto& stmt = frame.f.getStatement(stmtIndex);
        frame.stmtIndex += 1;

        switch(stmt.ty){
        case StatementType::Unreachable:{
            diagnostic(Diag::Error_Exec_Unreachable);
            throw std::runtime_error("Unreachable");
        }/*break;*/
        case StatementType::Assignment:{
            const AssignmentStatement& assign = frame.f.getAssignmentStatement(stmt.statementIndexInType);
            // evaluate the expression first
            ValueType rhsTy = ValueType::Void;
            QVariant rhsVal;
            bool isGood = evaluateExpression(assign.rvalueExprIndex, rhsTy, rhsVal);
            if(Q_UNLIKELY(!isGood)){
                throw std::runtime_error("Expression evaluation fail");
            }
            if(assign.lvalueExprIndex == -1){
                isGood = write(assign.lvalueName, rhsTy, rhsVal);
            }else{
                ValueType lhsTy = ValueType::Void;
                QVariant lhsVal;
                isGood = evaluateExpression(assign.lvalueExprIndex, lhsTy, lhsVal);
                if(Q_UNLIKELY(!isGood)){
                    throw std::runtime_error("Expression evaluation fail");
                }
                if(Q_UNLIKELY(lhsTy != ValueType::ValuePtr)){
                    diagnostic(Diag::Error_Exec_Assign_InvalidLHSType, lhsTy);
                    throw std::runtime_error("Expression type mismatch");
                }
                ValuePtrType ptr = lhsVal.value<ValuePtrType>();
                isGood = write(ptr, rhsTy, rhsVal);
            }
            if(Q_UNLIKELY(!isGood)){
                throw std::runtime_error("Expression evaluation fail");
            }
        }break;
        case StatementType::Output:{
            const OutputStatement& outstmt = frame.f.getOutputStatement(stmt.statementIndexInType);
            ValueType rhsTy = ValueType::Void;
            QVariant rhsVal;
            bool isGood = evaluateExpression(outstmt.exprIndex, rhsTy, rhsVal);
            if(Q_UNLIKELY(!isGood)){
                throw std::runtime_error("Expression evaluation fail");
            }
            if(Q_LIKELY(allowedOutputTypes.contains(rhsTy))){
                bool isGood = true;
                switch (rhsTy) {
                default: Q_UNREACHABLE();
                case ValueType::String:
                    isGood = out.addOutput(rhsVal.toString());
                    break;
                }
                if(Q_UNLIKELY(!isGood)){
                    diagnostic(Diag::Error_Exec_Output_Unknown_String, rhsVal.toString());
                    throw std::runtime_error("Output failure");
                }
            }else{
                diagnostic(Diag::Error_Exec_Output_InvalidType, rhsTy);
                throw std::runtime_error("Invalid output expression type");
            }
        }break;
        case StatementType::Call:{
            const CallStatement& call = frame.f.getCallStatement(stmt.statementIndexInType);
            int functionIndex = t.getFunctionIndex(call.functionName);
            if(Q_UNLIKELY(functionIndex < 0)){
                diagnostic(Diag::Error_Exec_Call_BadReference, call.functionName);
                throw std::runtime_error("Function not found");
            }
            const Function& f = t.getFunction(functionIndex);
            int numParam = f.getNumParameter();
            int numRequiredParam = f.getNumRequiredParameter();
            int numPassed = call.argumentExprList.size();
            if(Q_UNLIKELY(numPassed > numParam || numPassed < numRequiredParam)){
                diagnostic(Diag::Error_Exec_Call_BadArgumentList_Count, call.functionName, numRequiredParam, numParam, numPassed);
                throw std::runtime_error("Invalid call");
            }else{
                QList<QVariant> params;
                params.reserve(numPassed);
                for(int i = 0; i < numPassed; ++i){
                    params.push_back(QVariant());
                    int exprIndex = call.argumentExprList.at(i);
                    ValueType ty = ValueType::Void;
                    bool isGood = evaluateExpression(exprIndex, ty, params.back());
                    if(Q_UNLIKELY(!isGood)){
                        throw std::runtime_error("Expression evaluation fail");
                    }
                    if(Q_UNLIKELY(ty != f.getLocalVariableType(i))){
                        //Error_Exec_Call_BadArgumentList_Type
                        diagnostic(Diag::Error_Exec_Call_BadArgumentList_Type, call.functionName, i, f.getLocalVariableName(i), f.getLocalVariableType(i), ty);
                        throw std::runtime_error("Type mismatch");
                    }
                }
                pushFunctionStackframe(functionIndex, frame.irNodeIndex, params);
                // WARNING: frame should no longer be accessed, since pushing another stack frame may cause a relocation
            }
        }break;
        case StatementType::Branch:{
            const BranchStatement& branch = frame.f.getBranchStatement(stmt.statementIndexInType);
            bool isHandled = false;
            int labelAddress = -3;
            int caseIndex = -2;
            for(int i = 0, num = branch.cases.size(); i < num; ++i){
                const auto& brCase = branch.cases.at(i);
                ValueType ty = ValueType::Void;
                QVariant val;
                bool isGood = evaluateExpression(brCase.exprIndex, ty, val);
                if(Q_UNLIKELY(!isGood)){
                    throw std::runtime_error("Expression evaluation fail");
                }
                switch (ty) {
                default:{
                    diagnostic(Diag::Error_Exec_Branch_InvalidConditionType, i, ty);
                    throw std::runtime_error("Type mismatch");
                }/*break;*/
                case ValueType::Int64:{
                    if(val.toLongLong() != 0){
                        isHandled = true;
                        labelAddress = brCase.stmtIndex;
                        caseIndex = i;
                        break;
                    }
                }break;
                case ValueType::ValuePtr:{
                    if(val.value<ValuePtrType>().ty != ValuePtrType::PtrType::NullPointer){
                        isHandled = true;
                        labelAddress = brCase.stmtIndex;
                        caseIndex = i;
                        break;
                    }
                }
                }
                if(isHandled)
                    break;
            }
            if(!isHandled){
                caseIndex = -1;
                labelAddress = branch.defaultStmtIndex;
            }
            if(Q_UNLIKELY(labelAddress < -2 || labelAddress >= frame.f.getNumStatement())){
                diagnostic(Diag::Error_Exec_Branch_InvalidLabelAddress, caseIndex, labelAddress);
                throw std::runtime_error("Invalid label");
            }
            if(labelAddress >= 0){
                frame.stmtIndex = labelAddress;
            }else if(Q_UNLIKELY(labelAddress == -2)){
                diagnostic(Diag::Error_Exec_Branch_Unreachable, caseIndex);
                throw std::runtime_error("Unreachable");
            }
            // labelIndex == -1 is fall-through
        }break;
        case StatementType::Return:{
            stack.pop();
        }break;
        }// end of switch of statement type
    }// end of for loop
}

bool ExecutionContext::evaluateExpression(int expressionIndex, ValueType& ty, QVariant& val)
{
    Q_ASSERT(!stack.empty());
    const auto& frame = stack.top();

    const ExpressionBase* expr = frame.f.getExpression(expressionIndex);
    QList<int> dependencies;
    QList<ValueType> dependTys;
    expr->getDependency(dependencies, dependTys);
    QList<QVariant> dependentVals;
    Q_ASSERT(dependencies.size() == dependTys.size());
    dependentVals.reserve(dependencies.size());
    for(int i = 0, num = dependencies.size(); i < num; ++i){
        dependentVals.push_back(QVariant());
        ValueType actualTy = ValueType::Void;
        if(Q_UNLIKELY(!evaluateExpression(dependencies.at(i), actualTy, dependentVals.back())))
            return false;
        if(Q_UNLIKELY(actualTy != dependTys.at(i))){
            diagnostic(Diag::Error_Exec_TypeMismatch_ExpressionDependency,
                       dependTys.at(i), actualTy, expressionIndex, dependencies.at(i));
            return false;
        }
    }
    ty = expr->getExpressionType();
    return expr->evaluate(*this, val, dependentVals);
}

QString ExecutionContext::getNodeDescription(int nodeIndex)
{
    if(nodeIndex < 0){
        return tr("invalid node");
    }
    QString result = tr("node %1 ~").arg(nodeIndex);
    QStringList path;
    int currentNodeIndex = nodeIndex;
    while(currentNodeIndex > 0){
        const IRNodeInstance& inst = root.getNode(currentNodeIndex);
        const IRNodeType& ty = root.getType().getNodeType(inst.getTypeIndex());
        const QString& name = ty.getName();
        int parent = inst.getParentIndex();
        const IRNodeInstance& parentInst = root.getNode(parent);
        const IRNodeType& parentTy = root.getType().getNodeType(parentInst.getTypeIndex());
        int localTyIndex = parentTy.getChildNodeTypeIndex(name);
        int index = 0;
        int numInst = parentInst.getNumChildNodeUnderType(localTyIndex);
        while(index < numInst){
            if(parentInst.getChildNodeIndex(localTyIndex, index) == currentNodeIndex)
                break;
            ++index;
        }
        Q_ASSERT(index < numInst);
        path.push_back(tr("/%1[%2]").arg(name,QString::number(index)));

        currentNodeIndex = parent;
    }
    for(int i = path.size() -1; i >= 0; --i){
        result.append(path.at(i));
    }
    return result;
}

QString ExecutionContext::getPointerSrcDescription(const PtrCommon& head)
{
    return tr("[ptr created from function %1 [%2], statement %3]").arg(
                t.getFunction(head.functionIndex).getName(),
                QString::number(head.activationIndex),
                QString::number(head.stmtIndex));
}

QString ExecutionContext::getValuePtrDescription(const ValuePtrType& ptr)
{
    QString result;
    switch(ptr.ty){
    case ValuePtrType::PtrType::NullPointer:{
        result = tr("null");
    }break;
    case ValuePtrType::PtrType::LocalVariable:{
        const Function& f = t.getFunction(ptr.head.functionIndex);
        result = tr("&%1 in %2() [%3]").arg(
                    f.getLocalVariableName(ptr.valueIndex),
                    f.getName(),
                    QString::number(ptr.head.activationIndex));
    }break;
    case ValuePtrType::PtrType::NodeRWMember:{
        const IRNodeInstance& inst = root.getNode(ptr.nodeIndex);
        result = tr("&%1 in %2").arg(
                    t.getNodeMemberName(inst.getTypeIndex(), ptr.valueIndex),
                    getNodeDescription(ptr.nodeIndex));
    }break;
    case ValuePtrType::PtrType::NodeROParameter:{
        const IRNodeInstance& inst = root.getNode(ptr.nodeIndex);
        const IRNodeType& ty = root.getType().getNodeType(inst.getTypeIndex());
        result = tr("&%1 in %2").arg(
                    ty.getParameterName(ptr.valueIndex),
                    getNodeDescription(ptr.nodeIndex));
    }break;
    case ValuePtrType::PtrType::GlobalVariable:{
        result = tr("&%1").arg(t.getGlobalVariableName(ptr.valueIndex));
    }break;
    }
    result.append(' ');
    result.append(getPointerSrcDescription(ptr.head));
    return result;
}

QString ExecutionContext::getNodePtrDescription(const NodePtrType& ptr)
{
    return tr("&[%1] ").arg(getNodeDescription(ptr.nodeIndex)) + getPointerSrcDescription(ptr.head);
}
