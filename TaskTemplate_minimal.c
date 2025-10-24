/*

  A minimal template for a task running in the TISM-system; all comments, debugging and logging removed.

  Copyright (c) 2024 Maarten Klarenbeek (https://github.com/mjklaren)
  Distributed under the GPLv3 license

*/

#include "TISM.h"


struct TaskTemplateData
{
  int YourVariable1, YourVariable2;  
} TaskTemplateData;


uint8_t TaskTemplate (TISM_Task ThisTask)
{
  switch(ThisTask.TaskState)   
  {
    case INIT:  TaskTemplateData.YourVariable1=11;
                TaskTemplateData.YourVariable2=22;
                // TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_SLEEP,true);
				        break;
	  case RUN:   uint8_t MessageCounter=0;
                TISM_Message *MessageToProcess;
                while((TISM_PostmanTaskMessagesWaiting(ThisTask)>0) && (MessageCounter<MAX_MESSAGES))
                {
                  MessageToProcess=TISM_PostmanTaskReadMessage(ThisTask);
                  switch(MessageToProcess->MessageType)
                  {
                    case TISM_PING: TISM_PostmanTaskWriteMessage(ThisTask,MessageToProcess->SenderHostID,MessageToProcess->SenderTaskID,TISM_ECHO,MessageToProcess->Message,0);
                                    break;
                    default:        break;
                  }
                  TISM_PostmanTaskDeleteMessage(ThisTask);
                  MessageCounter++;
                }
				        break;
	  case STOP:  TISM_TaskManagerSetMyTaskAttribute(ThisTask,TISM_SET_TASK_STATE,DOWN);
		            break;					
  }
  return (OK);
}

