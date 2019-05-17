// Copyright (c) 2012-2019 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef BASE_TRANSFERTASK_H_
#define BASE_TRANSFERTASK_H_

#include <base/DownloadTask.h>
#include <base/UploadTask.h>

class TransferTask {

public:

    enum TransferTaskType {
        DOWNLOAD_TASK,
        UPLOAD_TASK
    };

    TransferTask(DownloadTask * ptr_downloadTask)
        : type(DOWNLOAD_TASK),
          p_downloadTask(ptr_downloadTask),
          p_uploadTask(0),
          m_remove(false)
    {
    }

    TransferTask(UploadTask * ptr_uploadTask)
        : type(UPLOAD_TASK),
          p_downloadTask(0),
          p_uploadTask(ptr_uploadTask),
          m_remove(false)
    {
    }

    virtual ~TransferTask()
    {
        if (p_downloadTask)
            delete p_downloadTask;
        if (p_uploadTask)
            delete p_uploadTask;
    }

    void setLocationHeader(const std::string& s)
    {
        if (p_downloadTask)
            p_downloadTask->setLocationHeader(s);
        if (p_uploadTask)
            p_uploadTask->setReplyLocation(s);
    }

    TransferTaskType type;
    DownloadTask* p_downloadTask;
    UploadTask* p_uploadTask;
    bool m_remove;

};

#endif /*BASE_TRANSFERTASK_H_*/
