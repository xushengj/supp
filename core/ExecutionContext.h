#ifndef EXECUTIONCONTEXT_H
#define EXECUTIONCONTEXT_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QStack>
#include <QEventLoop>

#include <memory>

#include "core/Value.h"

class DiagnosticEmitterBase;
class OutputHandlerBase;
class Function;
class Task;
class IRRootInstance;

// you probably want to move ExecutionContext to another thread

class ExecutionContext: public QObject
{
    Q_OBJECT
public:
    ExecutionContext(const Task& t, const IRRootInstance& root, DiagnosticEmitterBase& diagnostic, OutputHandlerBase& out, QObject* parent = nullptr);
    virtual ~ExecutionContext() override{}

    // interface exposed to everyone
    const Task& getTask()const{return t;}
    DiagnosticEmitterBase& getDiagnostic(){return diagnostic;}

    QString getNodeDescription(int nodeIndex);
    QString getPointerSrcDescription(const PtrCommon& head);
    QString getValuePtrDescription(const ValuePtrType& ptr);
    QString getNodePtrDescription(const NodePtrType& ptr);

    //*************************************************************************
    // interface exposed to ExpressionBase
    // there must be a stack frame set
    /**
     * @brief read read from local variable, node member, or global variable by name reference
     * @param name variable name for lookup
     * @param ty value type of result
     * @param dest read value
     * @return true if read is successful; false otherwise
     */
    bool read(const QString& name, ValueType& ty, QVariant& val);

    /**
     * @brief read read a value by dereferencing a pointer
     * @param valuePtr pointer to dereference
     * @param ty value type of result
     * @param dest read value
     * @return true if read is successful; false otherwise
     */
    bool read(const ValuePtrType& valuePtr, ValueType& ty, QVariant& dest);

    /**
     * @brief takeAddress create a pointer from variable name lookup
     * @param name variable name for lookup
     * @param val pointer value
     * @return true if lookup successful; false otherwise
     */
    bool takeAddress(const QString& name, ValuePtrType& val);

    // used to construct node reference
    // these three never fails
    bool getCurrentNodePtr(NodePtrType& result);
    bool getRootNodePtr(NodePtrType& result);
    bool getParentNode(const NodePtrType& src, NodePtrType& result);

    bool getChildNode(int nodeIndex, const QString& childName, NodePtrType& result, ValueType keyTy, const QVariant& primaryKey);
    bool getChildNode(int nodeIndex, const QString& childName, NodePtrType& result, const QString& keyField,  ValueType keyTy, const QVariant& keyValue);
    // no indexing by child node index yet.. should be there later on

    //*************************************************************************
    // interface exposed to environment

    /**
     * @brief addBreakpoint adds a breakpoint at specified location. Duplicate breakpoints are ignored
     * @param functionIndex index of the function to break at
     * @param stmtIndex index of the statement to break at
     * @return index of breakpoint; if there is a duplicate then the index of existing one is added
     */
    int addBreakpoint(int functionIndex, int stmtIndex);
    /**
     * @brief removeBreakpoint removes the specified breakpoint. If index is -1 then all breakpoints are removed
     * @param breakpointIndex the index of breakpoint to remove
     */
    void removeBreakpoint(int breakpointIndex);

signals:
    void executionFinished(int retval);// 0: success; -1: fail
    void executionPaused();
public slots:
    void continueExecution();

private:
    void mainExecutionEntry();
    void nodeTraverseEntry(int passIndex, int nodeIndex);
    void pushFunctionStackframe(int functionIndex, int nodeIndex, QList<QVariant> params = QList<QVariant>());
    void functionMainLoop();

    void checkUninitializedRead(ValueType ty, QVariant& readVal);
    bool write(const QString& name, const ValueType& ty, const QVariant& val);
    bool write(const ValuePtrType& valuePtr, const ValueType& ty, const QVariant& dest);
    /**
     * @brief evaluateExpression evaluates the expression in current stack frame's current function
     * @param expressionIndex the root expression index to evaluate
     * @param ty the final type of root expression
     * @param val the final value of root expression
     * @return true if the evaluation is successful, false otherwise
     */
    bool evaluateExpression(int expressionIndex, int stmtIndex, ValueType& ty, QVariant& val);



    PtrCommon getPtrSrcHead(){
        PtrCommon result;
        const auto& frame = *stack.top();
        result.functionIndex = frame.functionIndex;
        result.activationIndex = frame.activationIndex;
        result.stmtIndex = frame.stmtIndex;
        return result;
    }

private:
    struct CallStackEntry{
        const Function& f;
        const int functionIndex;
        const int irNodeIndex;
        const int irNodeTypeIndex;    //!< node global type index
        const int activationIndex;    //!< detect dangling pointer to stack variable
        int stmtIndex;
        QList<QVariant> localVariables;
        CallStackEntry(const Function& f, int functionIndex, int nodeIndex, int nodeTypeIndex, int activationIndex)
            : f(f),
              functionIndex(functionIndex),
              irNodeIndex(nodeIndex),
              irNodeTypeIndex(nodeTypeIndex),
              activationIndex(activationIndex),
              stmtIndex(0)
        {}
        CallStackEntry(const CallStackEntry&) = default;
        CallStackEntry(CallStackEntry&&) = default;
    };
    struct BreakPoint{
        int functionIndex;
        int stmtIndex;
    };
    struct TraverseState{
        enum class NodeTraverseState{
            Entry,
            Traverse,
            Exit
        };
        NodeTraverseState nodeState;
        int passIndex;
        int nodeIndex;
        int childIndex;
    };

    // states
    QStack<std::shared_ptr<CallStackEntry>> stack;
    QList<QVariant> globalVariables;
    QList<QList<QVariant>> nodeMembers;// read-writeable variables only; constant ones are still in IRNodeInstance
    QStack<TraverseState> nodeTraverseStack;
    int currentActivationCount = 0;
    bool isInExecution = false;

    QHash<int, BreakPoint> breakpoints;
    bool isBreakpointUpdated = false;   //!< set if breakpoints are changed

    QList<ValueType> allowedOutputTypes;
    QEventLoop eventLoop;

    // references
    const Task& t;
    const IRRootInstance& root;
    DiagnosticEmitterBase& diagnostic;
    OutputHandlerBase& out;
};

#endif // EXECUTIONCONTEXT_H
