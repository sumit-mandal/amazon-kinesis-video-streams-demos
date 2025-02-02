/**
 * Kinesis Video Producer cloudwatch logging
 */
#pragma once

#include "CanaryLogs.h"

#define LOG_CLASS "CanaryStreamLogs"

LOGGER_TAG("com.amazonaws.kinesis.video");

CanaryLogs::PCloudwatchLogsObject gCloudwatchLogsObject = NULL;
std::mutex gLogLock; // Protect access to gCloudwatchLogsObject

CanaryLogs::CanaryLogs(){};
CanaryLogs::CloudwatchLogsObject::CloudwatchLogsObject(){};

STATUS CanaryLogs::initializeCloudwatchLogger(PCloudwatchLogsObject pCloudwatchLogsObject)
{
    STATUS retStatus = STATUS_SUCCESS;
    Aws::CloudWatchLogs::Model::CreateLogStreamOutcome createLogStreamOutcome;
    CHK(pCloudwatchLogsObject != NULL, STATUS_NULL_ARG);
    pCloudwatchLogsObject->canaryLogGroupRequest.SetLogGroupName(pCloudwatchLogsObject->logGroupName);
    pCloudwatchLogsObject->pCwl->CreateLogGroup(pCloudwatchLogsObject->canaryLogGroupRequest);

    pCloudwatchLogsObject->canaryLogStreamRequest.SetLogStreamName(pCloudwatchLogsObject->logStreamName);
    pCloudwatchLogsObject->canaryLogStreamRequest.SetLogGroupName(pCloudwatchLogsObject->logGroupName);
    createLogStreamOutcome = pCloudwatchLogsObject->pCwl->CreateLogStream(pCloudwatchLogsObject->canaryLogStreamRequest);
    CHK_ERR(createLogStreamOutcome.IsSuccess(), STATUS_INVALID_OPERATION, "Failed to create \"%s\" log stream: %s",
            pCloudwatchLogsObject->logStreamName.c_str(), createLogStreamOutcome.GetError().GetMessage().c_str());
    gCloudwatchLogsObject = pCloudwatchLogsObject;
CleanUp:
    return retStatus;
}

VOID CanaryLogs::setUpLogEventVector(PCHAR logString)
{
    std::lock_guard<std::mutex> lock(gLogLock);
    if(gCloudwatchLogsObject != NULL) {
        std::unique_lock<std::recursive_mutex> lock(gCloudwatchLogsObject->mutex);
        Aws::String awsCwString((Aws::String) logString);
        auto logEvent =
            Aws::CloudWatchLogs::Model::InputLogEvent().WithMessage(awsCwString).WithTimestamp(GETTIME() / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
        gCloudwatchLogsObject->canaryInputLogEventVec.push_back(logEvent);
    }
}

VOID CanaryLogs::onPutLogEventResponseReceivedHandler(const Aws::CloudWatchLogs::CloudWatchLogsClient* cwClientLog,
                                          const Aws::CloudWatchLogs::Model::PutLogEventsRequest& request,
                                          const Aws::CloudWatchLogs::Model::PutLogEventsOutcome& outcome,
                                          const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context)
{
    if (!outcome.IsSuccess()) {
        LOG_ERROR("Failed to push logs: " << outcome.GetError().GetMessage().c_str());
    } else {
        LOG_DEBUG("Successfully pushed logs to cloudwatch");
        gCloudwatchLogsObject->token = outcome.GetResult().GetNextSequenceToken();
    }
}

VOID CanaryLogs::canaryStreamSendLogs(PCloudwatchLogsObject pCloudwatchLogsObject)
{
    std::unique_lock<std::recursive_mutex> lock(pCloudwatchLogsObject->mutex);
    Aws::CloudWatchLogs::Model::PutLogEventsOutcome outcome;
    auto request = Aws::CloudWatchLogs::Model::PutLogEventsRequest()
                       .WithLogGroupName(pCloudwatchLogsObject->logGroupName)
                       .WithLogStreamName(pCloudwatchLogsObject->logStreamName)
                       .WithLogEvents(pCloudwatchLogsObject->canaryInputLogEventVec);
    if (pCloudwatchLogsObject->token != "") {
        request.SetSequenceToken(pCloudwatchLogsObject->token);
    }
    pCloudwatchLogsObject->pCwl->PutLogEventsAsync(request, onPutLogEventResponseReceivedHandler);
    pCloudwatchLogsObject->canaryInputLogEventVec.clear();
}

VOID CanaryLogs::canaryStreamSendLogSync(PCloudwatchLogsObject pCloudwatchLogsObject)
{
    std::unique_lock<std::recursive_mutex> lock(pCloudwatchLogsObject->mutex);
    auto request = Aws::CloudWatchLogs::Model::PutLogEventsRequest()
                       .WithLogGroupName(pCloudwatchLogsObject->logGroupName)
                       .WithLogStreamName(pCloudwatchLogsObject->logStreamName)
                       .WithLogEvents(pCloudwatchLogsObject->canaryInputLogEventVec);
    if (pCloudwatchLogsObject->token != "") {
        request.SetSequenceToken(pCloudwatchLogsObject->token);
    }
    auto outcome = pCloudwatchLogsObject->pCwl->PutLogEvents(request);
    if (!outcome.IsSuccess()) {
        LOG_ERROR("Failed to push logs: " << outcome.GetError().GetMessage().c_str());
    } else {
        LOG_DEBUG("Successfully pushed logs to cloudwatch");
    }
    pCloudwatchLogsObject->canaryInputLogEventVec.clear();
}