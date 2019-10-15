#ifndef BUNDLE_H
#define BUNDLE_H

#include <QList>

#include "core/OutputHandlerBase.h"

class IRRootType;
class IRRootInstance;
class Task;

// work in progress

class Bundle
{
    Q_DECLARE_TR_FUNCTIONS(Bundle)
public:
    Bundle(){}

    int getNumIR()const{return irTypes.size();}
    int getNumTask()const{return taskInfo.size();}
    const IRRootType& getIR(int irIndex)const{return *irTypes.at(irIndex);}
    const Task& getTask(int taskIndex)const{return *taskInfo.at(taskIndex).ptr;}

    void readFromJson(const QByteArray& json);
    void writeToJson(QByteArray& json);
    IRRootInstance* readIRFromJson(int irIndex, const QByteArray& json);

private:
    struct TaskRecord{
        Task* ptr;          //!< ptr to task object
        int inputIRType;    //!< which input type the task can work on
        enum class TaskOutputType{
            NoOutput,       //!< no output (e.g. validation)
            IR,             //!< the task performs IR-to-IR transform
            External,       //!< the task outputs external format
        };
        int outputTypeIndex;//!< index in outputTypes if it writes External output, index in irTypes if it writes IR output
    };
    QList<OutputDescriptor> outputTypes;
    QStringList outputNames;
    QList<IRRootType*> irTypes;
    QList<TaskRecord> taskInfo;
};

#endif // BUNDLE_H
