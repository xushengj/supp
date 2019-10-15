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
    const auto& frame = *stack.top();

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

    diagnostic.error(tr("Variable not found"),
                     tr("Value read fail: variable with the given name cannot be found"),
                     name);
    return false;
}

bool ExecutionContext::read(const ValuePtrType& valuePtr, ValueType& ty, QVariant& val)
{
    Q_ASSERT(!stack.empty());
    const auto& frame = *stack.top();// index is stack.size()-1

    switch(valuePtr.ty){
    case ValuePtrType::PtrType::NullPointer:{
        diagnostic.error(tr("Null pointer dereference"),
                         tr("Dereferencing a null pointer"),
                         getValuePtrDescription(valuePtr));
        return false;
    }/*break;*/
    case ValuePtrType::PtrType::LocalVariable:{
        int ptrAcivationIndex = valuePtr.head.activationIndex;
        if(frame.activationIndex != ptrAcivationIndex){
            // walk over the stack to see if the activation is still alive
            for(int i = stack.size()-2; i >= 0; ++i){
                const auto& curFrame = *stack.at(i);
                if(curFrame.activationIndex == ptrAcivationIndex){
                    // okay we found it
                    ty = curFrame.f.getLocalVariableType(valuePtr.valueIndex);
                    val = curFrame.localVariables.at(valuePtr.valueIndex);
                    checkUninitializedRead(ty, val);
                    return true;
                }
            }
            diagnostic.error(tr("Dangling pointer dereference"),
                             tr("Dereferencing a pointer to local variable after the function is finished"),
                             getValuePtrDescription(valuePtr));
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
    diagnostic.warning(tr("Uninitialized read"),
                       tr("Value is read before initialized"));
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
        ptr.nodeIndex = 0;
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
    const auto& frame = *stack.top();

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

    diagnostic.error(tr("Variable not found"),
                     tr("Value read fail: variable with the given name cannot be found"),
                     name);
    return false;
}

bool ExecutionContext::write(const QString& name, const ValueType& ty, const QVariant& val)
{
    Q_ASSERT(!stack.empty());
    auto& frame = *stack.top();
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
            diagnostic.error(tr("Writing to read-only data"),
                             tr("Node parameter is read only"),
                             name);
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
        diagnostic.error(tr("Variable not found"),
                         tr("Value write fail: variable with the given name cannot be found"),
                         name);
        return false;
    }else if(Q_UNLIKELY(actualTy != ty)){
        diagnostic.error(tr("Type mismatch"),
                         tr("Variable to write is in type %1 but the data to write is in type %2").arg(
                             getTypeNameString(actualTy),
                             getTypeNameString(ty)
                           ),
                         name);
        return false;
    }else{
        *valPtr = val;
        return true;
    }
}

bool ExecutionContext::write(const ValuePtrType& valuePtr, const ValueType& ty, const QVariant& dest)
{
    Q_ASSERT(!stack.empty());
    auto& frame = *stack.top();// index is stack.size()-1
    ValueType actualTy = ValueType::Void;
    QVariant* valPtr = nullptr;

    switch(valuePtr.ty){
    case ValuePtrType::PtrType::NullPointer:{
        diagnostic.error(tr("Null pointer dereference"),
                         tr("Dereferencing a null pointer"),
                         getValuePtrDescription(valuePtr));
        return false;
    }/*break;*/
    case ValuePtrType::PtrType::LocalVariable:{
        int ptrAcivationIndex = valuePtr.head.activationIndex;
        if(frame.activationIndex != ptrAcivationIndex){
            // walk over the stack to see if the activation is still alive
            for(int i = stack.size()-2; i >= 0; ++i){
                auto& curFrame = *stack[i];
                if(curFrame.activationIndex == ptrAcivationIndex){
                    // okay we found it
                    actualTy = curFrame.f.getLocalVariableType(valuePtr.valueIndex);
                    valPtr = &curFrame.localVariables[valuePtr.valueIndex];
                    break;
                }
            }
            diagnostic.error(tr("Dangling pointer dereference"),
                             tr("Dereferencing a pointer to local variable after the function is finished"),
                             getValuePtrDescription(valuePtr));
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
        diagnostic.error(tr("Writing to read-only data"),
                         tr("Attempt to write by a pointer to read only node parameter"),
                         getValuePtrDescription(valuePtr));
        return false;
    }/*break;*/
    case ValuePtrType::PtrType::GlobalVariable:{
        actualTy = t.getGlobalVariableType(valuePtr.valueIndex);
        valPtr = &globalVariables[valuePtr.valueIndex];
    }break;
    }

    if(Q_UNLIKELY(actualTy != ty)){
        diagnostic.error(tr("Type mismatch"),
                         tr("Variable to write is in type %1 but the data to write is in type %2").arg(
                             getTypeNameString(actualTy),
                             getTypeNameString(ty)
                           ),
                         getValuePtrDescription(valuePtr));
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
    result.nodeIndex = stack.top()->irNodeIndex;
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
    result.head = getPtrSrcHead();
    result.nodeIndex = root.getNode(src.nodeIndex).getParentIndex();
    if(result.nodeIndex < 0){
        // must be the root node
        // set it to be itself silently
        Q_ASSERT(result.nodeIndex == -1);
        result.nodeIndex = 0;
    }
    return true;
}

bool ExecutionContext::getChildNode(int nodeIndex, const QString& childName, NodePtrType& result, ValueType keyTy, const QVariant& primaryKey)
{
    int childTyIndex = root.getType().getNodeIndex(childName);
    const IRNodeType& childTy = root.getType().getNodeType(childTyIndex);
    int primaryKeyIndex = childTy.getPrimaryKeyParameterIndex();
    if(Q_UNLIKELY(primaryKeyIndex < 0)){
        diagnostic.error(tr("Invalid node traversal"),
                         tr("Attempt to find child node by primary key but there is no primary key parameter"),
                         childName);
        return false;
    }
    if(Q_UNLIKELY(childTy.getParameterType(primaryKeyIndex) != keyTy)){
        diagnostic.error(tr("Type mismatch"),
                         tr("Primary key parameter is in type %1 but lookup uses %2").arg(
                             getTypeNameString(childTy.getParameterType(primaryKeyIndex)),
                             getTypeNameString(keyTy)
                           ),
                         childTy.getParameterName(primaryKeyIndex)
                         );
        return false;
    }
    const IRNodeInstance& inst = root.getNode(nodeIndex);
    int childTyLocalIndex = inst.getLocalTypeIndex(childTyIndex);
    int childIndex = inst.getChildNodeIndex(childTyLocalIndex, primaryKeyIndex, primaryKey);
    if(Q_UNLIKELY(childIndex < 0)){
        diagnostic.error(tr("Invalid node traversal"),
                         tr("No child node with given primary key is found"),
                         childName);
        return false;
    }

    result.head = getPtrSrcHead();
    result.nodeIndex = childIndex;
    return true;
}

bool ExecutionContext::getChildNode(int nodeIndex, const QString& childName, NodePtrType& result, const QString& keyField, ValueType keyTy, const QVariant& keyValue)
{
    int childTyIndex = root.getType().getNodeIndex(childName);
    const IRNodeType& childTy = root.getType().getNodeType(childTyIndex);
    int paramIndex = childTy.getParameterIndex(keyField);
    if(Q_UNLIKELY(paramIndex < 0)){
        diagnostic.error(tr("Invalid node traversal"),
                         tr("No such parameter under child node"),
                         keyField);
        return false;
    }
    if(Q_UNLIKELY(!childTy.getParameterIsUnique(paramIndex))){
        diagnostic.error(tr("Invalid node traversal"),
                         tr("Specified parameter is not marked unique and cannot be used for node lookup"),
                         keyField);
        return false;
    }
    if(Q_UNLIKELY(childTy.getParameterType(paramIndex) != keyTy)){
        diagnostic.error(tr("Type mismatch"),
                         tr("Key parameter is in type %1 but lookup uses %2").arg(
                             getTypeNameString(childTy.getParameterType(paramIndex)),
                             getTypeNameString(keyTy)
                           ),
                         keyField
                         );
        return false;
    }
    const IRNodeInstance& inst = root.getNode(nodeIndex);
    int childTyLocalIndex = inst.getLocalTypeIndex(childTyIndex);
    int childIndex = inst.getChildNodeIndex(childTyLocalIndex, paramIndex, keyValue);
    if(Q_UNLIKELY(childIndex < 0)){
        diagnostic.error(tr("Invalid node traversal"),
                         tr("No child node with given key is found"),
                         childName);
        return false;
    }

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
    const IRNodeType& ty = root.getType().getNodeType(inst.getTypeIndex());
    diagnostic.attachDescriptiveName(ty.getName());
    int entryCB = t.getNodeCallback(nodeIndex, Task::CallbackType::OnEntry, passIndex);
    int exitCB = t.getNodeCallback(nodeIndex, Task::CallbackType::OnExit, passIndex);

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
        CallStackEntry* entry = new CallStackEntry(f, functionIndex, nodeIndex, root.getNode(nodeIndex).getTypeIndex(), activationIndex);
        int localVariableCnt = f.getNumLocalVariable();
        entry->localVariables.reserve(localVariableCnt);
        for(int i = 0; i < localVariableCnt; ++i){
            entry->localVariables.push_back(f.getLocalVariableInitializer(i));
        }
        for(int i = 0, num = params.size(); i < num; ++i){
            entry->localVariables[i] = params.at(i);
        }
        std::shared_ptr<CallStackEntry> ptr(entry);
        stack.push(ptr);
    }
}

void ExecutionContext::functionMainLoop()
{
    while(!stack.empty()){
        auto& frame = *stack.top();
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
            diagnostic.error(tr("Unreachable statement execution"),
                             tr("Unreachable statement is executed"),
                             tr("Statement %1").arg(stmtIndex));
            throw std::runtime_error("Unreachable");
        }/*break;*/
        case StatementType::Assignment:{
            const AssignmentStatement& assign = frame.f.getAssignmentStatement(stmt.statementIndexInType);
            // evaluate the expression first
            ValueType rhsTy = ValueType::Void;
            QVariant rhsVal;
            bool isGood = evaluateExpression(assign.rvalueExprIndex, stmtIndex, rhsTy, rhsVal);
            if(Q_UNLIKELY(!isGood)){
                throw std::runtime_error("Expression evaluation fail");
            }
            if(assign.lvalueExprIndex == -1){
                isGood = write(assign.lvalueName, rhsTy, rhsVal);
            }else{
                ValueType lhsTy = ValueType::Void;
                QVariant lhsVal;
                isGood = evaluateExpression(assign.lvalueExprIndex, stmtIndex, lhsTy, lhsVal);
                if(Q_UNLIKELY(!isGood)){
                    throw std::runtime_error("Expression evaluation fail");
                }
                if(Q_UNLIKELY(lhsTy != ValueType::ValuePtr)){
                    diagnostic.error(tr("Invalid LHS expression"),
                                     tr("LHS of assignment evaluates to type %1 instead of value pointer").arg(
                                         getTypeNameString(lhsTy)));
                    throw std::runtime_error("Expression type mismatch");
                }
                ValuePtrType ptr = lhsVal.value<ValuePtrType>();
                isGood = write(ptr, rhsTy, rhsVal);
            }
            if(Q_UNLIKELY(!isGood)){
                diagnostic.error(tr("Assignment fail"),
                                 tr("Assignment statement fail to execute"),
                                 tr("Statement %1").arg(stmtIndex));
                throw std::runtime_error("Expression evaluation fail");
            }
        }break;
        case StatementType::Output:{
            const OutputStatement& outstmt = frame.f.getOutputStatement(stmt.statementIndexInType);
            ValueType rhsTy = ValueType::Void;
            QVariant rhsVal;
            bool isGood = evaluateExpression(outstmt.exprIndex, stmtIndex, rhsTy, rhsVal);
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
                    diagnostic.error(tr("Output failure"),
                                     tr("Output error occured"),
                                     rhsVal.toString());
                    throw std::runtime_error("Output failure");
                }
            }else{
                diagnostic.error(tr("Invalid output expression type"),
                                 tr("Task output do not support expression of type %1").arg(
                                     getTypeNameString(rhsTy)));
                throw std::runtime_error("Invalid output expression type");
            }
        }break;
        case StatementType::Call:{
            const CallStatement& call = frame.f.getCallStatement(stmt.statementIndexInType);
            int functionIndex = t.getFunctionIndex(call.functionName);
            if(Q_UNLIKELY(functionIndex < 0)){
                diagnostic.error(tr("Function not found"),
                                 tr("Function with the given name is not found"),
                                 call.functionName);
                throw std::runtime_error("Function not found");
            }
            const Function& f = t.getFunction(functionIndex);
            int numParam = f.getNumParameter();
            int numRequiredParam = f.getNumRequiredParameter();
            int numPassed = call.argumentExprList.size();
            if(Q_UNLIKELY(numPassed > numParam || numPassed < numRequiredParam)){
                QString category = tr("Too many parameters for call");
                QString msg;
                if(numPassed > numParam){
                    msg = tr("Function expects at most %1 parameters but call provides %2").arg(
                                QString::number(numParam),
                                QString::number(numPassed)
                             );
                }else{
                    Q_ASSERT(numPassed < numRequiredParam);
                    msg = tr("Function expects at least %1 parameters but call provides %2").arg(
                                QString::number(numRequiredParam),
                                QString::number(numPassed)
                             );
                }
                diagnostic.error(category,
                                 msg,
                                 tr("Statement %1, calling %2").arg(
                                     QString::number(stmtIndex),
                                     call.functionName)
                                 );
                throw std::runtime_error("Invalid call");
            }else{
                QList<QVariant> params;
                params.reserve(numPassed);
                for(int i = 0; i < numPassed; ++i){
                    params.push_back(QVariant());
                    int exprIndex = call.argumentExprList.at(i);
                    ValueType ty = ValueType::Void;
                    bool isGood = evaluateExpression(exprIndex, stmtIndex, ty, params.back());
                    if(Q_UNLIKELY(!isGood)){
                        throw std::runtime_error("Expression evaluation fail");
                    }
                    if(Q_UNLIKELY(ty != f.getLocalVariableType(i))){
                        diagnostic.error(tr("Type mismatch"),
                                         tr("Function argument %1 should be in type %2 but expression evaluates to type %3").arg(
                                             f.getLocalVariableName(i),
                                             getTypeNameString(f.getLocalVariableType(i)),
                                             getTypeNameString(ty)
                                            )
                                         );
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
            for(int i = 0, num = branch.cases.size(); i < num; ++i){
                const auto& brCase = branch.cases.at(i);
                ValueType ty = ValueType::Void;
                QVariant val;
                bool isGood = evaluateExpression(brCase.exprIndex, stmtIndex, ty, val);
                if(Q_UNLIKELY(!isGood)){
                    throw std::runtime_error("Expression evaluation fail");
                }
                switch (ty) {
                default:{
                    diagnostic.error(tr("Invalid branch condition type"),
                                     tr("Unhandled type %1 for branch case condition").arg(
                                         getTypeNameString(ty)));
                    throw std::runtime_error("Type mismatch");
                }/*break;*/
                case ValueType::Int64:{
                    if(val.toLongLong() != 0){
                        isHandled = true;
                        labelAddress = brCase.stmtIndex;
                        break;
                    }
                }break;
                case ValueType::ValuePtr:{
                    if(val.value<ValuePtrType>().ty != ValuePtrType::PtrType::NullPointer){
                        isHandled = true;
                        labelAddress = brCase.stmtIndex;
                        break;
                    }
                }
                }
                if(isHandled)
                    break;
            }
            if(!isHandled){
                labelAddress = branch.defaultStmtIndex;
            }
            if(Q_UNLIKELY(labelAddress < -2 || labelAddress >= frame.f.getNumStatement())){
                diagnostic.error(tr("Invalid label"),
                                 tr("Branch label address %1 is invalid").arg(
                                     QString::number(labelAddress)));
                throw std::runtime_error("Invalid label");
            }
            if(labelAddress >= 0){
                frame.stmtIndex = labelAddress;
            }else if(Q_UNLIKELY(labelAddress == -2)){
                diagnostic.error(tr("Unreachable case in branch"),
                                 tr("Branch reaches unreachable case"));
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

bool ExecutionContext::evaluateExpression(int expressionIndex, int stmtIndex, ValueType& ty, QVariant& val)
{
    Q_ASSERT(!stack.empty());
    const auto& frame = *stack.top();

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
        if(Q_UNLIKELY(!evaluateExpression(dependencies.at(i), stmtIndex, actualTy, dependentVals.back())))
            return false;
        if(Q_UNLIKELY(actualTy != dependTys.at(i))){
            diagnostic.error(tr("Type mismatch"),
                             tr("Expression expects type %1 but got %2").arg(
                                 getTypeNameString(dependTys.at(i)),
                                 getTypeNameString(actualTy)
                               ),
                             tr("Statement %1, expression %2 depended by %3").arg(
                                 QString::number(stmtIndex),
                                 QString::number(dependencies.at(i)),
                                 QString::number(expressionIndex)
                                 )
                             );
            return false;
        }
    }
    ty = expr->getExpressionType();
    return expr->evaluate(*this, val, dependentVals);
}

QString ExecutionContext::getNodeDescription(int nodeIndex)
{
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
