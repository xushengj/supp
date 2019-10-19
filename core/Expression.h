#ifndef EXPRESSION_H
#define EXPRESSION_H

#include <QtGlobal>
#include <QString>
#include <QVariant>
#include <QList>

#include "core/Value.h"

class ExecutionContext;

/**
 * @brief The ExpressionBase class is an interface for any expression that do not have side effects
 */
class ExpressionBase
{
    Q_DECLARE_TR_FUNCTIONS(ExpressionBase)
public:
    virtual ~ExpressionBase(){}

    /**
     * @brief clone return a new instance of expression, needed when copying parent functions around
     * @return pointer to new instance
     */
    virtual ExpressionBase* clone() const = 0;

    virtual ValueType getExpressionType() const = 0;

    /**
     * @brief getVariableNameReference populates a list of variable names that resolving this expression would need to lookup
     * @param name the list of variable names to populate
     */
    virtual void getVariableNameReference(QList<QString>& name) const {Q_UNUSED(name)}

    /**
     * @brief getDependency get list of expression indices that this expression depends upon
     * @param dependentExprIndexList
     */
    virtual void getDependency(QList<int>& dependentExprIndexList, QList<ValueType>& exprTypeList) const{
        Q_UNUSED(dependentExprIndexList)
        Q_UNUSED(exprTypeList)
    }
    /**
     * @brief evaluate evaluates the expression
     * @param ctx the environment of evaluation
     * @param retVal the return value
     * @param dependentExprResults dependent expression evaluation results
     * @return true if execution is successful; false if there is any fatal error that should abort the evaluation
     */
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const = 0;
};

// an expression list that use deep copy
class ExprList : public QList<ExpressionBase*>
{
public:
    ExprList(): QList<ExpressionBase*>(){}
    ~ExprList(){
        for(ExpressionBase* ptr : *this){
            delete ptr;
        }
    }
    ExprList(ExprList&&) = default;
    ExprList(const ExprList& rhs){
        clear();
        for(ExpressionBase* ptr : rhs){
            push_back(ptr->clone());
        }
    }
};

/**
 * @brief The LiteralExpression class
 *
 * Literal value as expression
 */
class LiteralExpression: public ExpressionBase
{
public:
    explicit LiteralExpression(ValueType ty, QVariant val)
        : ty(ty), val(val)
    {}
    explicit LiteralExpression(qint64 val)
        : ty(ValueType::Int64),
          val(val)
    {}
    explicit LiteralExpression(QString str)
        : ty(ValueType::String),
          val(str)
    {}
    virtual ~LiteralExpression() override{}
    virtual LiteralExpression* clone() const override{return new LiteralExpression(ty, val);}
    virtual ValueType getExpressionType() const override {return ty;}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    ValueType ty;
    QVariant val;
};

/**
 * @brief The VariableAddressExpression class
 *
 * Create a pointer from variable name lookup
 */
class VariableAddressExpression: public ExpressionBase
{
public:
    explicit VariableAddressExpression(QString varName)
        : variableName(varName)
    {}
    virtual ~VariableAddressExpression() override {}
    virtual VariableAddressExpression* clone() const override{return new VariableAddressExpression(variableName);}
    virtual ValueType getExpressionType() const override {return ValueType::ValuePtr;}
    virtual void getVariableNameReference(QList<QString>& name) const override {name.push_back(variableName);}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    QString variableName;
};

/**
 * @brief The VariableReadExpression class
 *
 * Just read the value by name
 */
class VariableReadExpression: public ExpressionBase
{
public:
    explicit VariableReadExpression(ValueType ty, QString varName)
        : ty(ty), variableName(varName)
    {}
    virtual ~VariableReadExpression() override {}
    virtual VariableReadExpression* clone() const override {return new VariableReadExpression(ty, variableName);}
    virtual ValueType getExpressionType() const override {return ty;}
    virtual void getVariableNameReference(QList<QString>& name) const override {name.push_back(variableName);}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    ValueType ty;
    QString variableName;
};

/**
 * @brief The NodeReferenceExpression class
 *
 * Create a node pointer
 */
class NodePtrExpression: public ExpressionBase
{
public:
    enum class NodeSpecifier{
        CurrentNode,
        RootNode
    };
    explicit NodePtrExpression(NodeSpecifier specifier)
        : specifier(specifier)
    {}
    virtual ~NodePtrExpression() override {}
    virtual NodePtrExpression* clone() const override {return new NodePtrExpression(specifier);}
    virtual ValueType getExpressionType() const override {return ValueType::NodePtr;}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    NodeSpecifier specifier;
};

#endif // EXPRESSION_H
