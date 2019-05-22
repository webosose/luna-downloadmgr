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

enum TransferTaskType {
    TransferTaskType_DOWNLOAD,
    TransferTaskType_UPLOAD
};

class TransferTask {
public:
    TransferTask(DownloadTask * ptr_downloadTask)
        : m_type(TransferTaskType_DOWNLOAD),
          m_downloadTask(ptr_downloadTask),
          m_uploadTask(0),
          m_remove(false)
    {
    }

    TransferTask(UploadTask * ptr_uploadTask)
        : m_type(TransferTaskType_UPLOAD),
          m_downloadTask(0),
          m_uploadTask(ptr_uploadTask),
          m_remove(false)
    {
    }

    virtual ~TransferTask()
    {
        if (m_downloadTask)
            delete m_downloadTask;
        if (m_uploadTask)
            delete m_uploadTask;
    }

    void setLocationHeader(const std::string& s)
    {
        if (m_downloadTask)
            m_downloadTask->setLocationHeader(s);
        if (m_uploadTask)
            m_uploadTask->setReplyLocation(s);
    }

    TransferTaskType m_type;
    DownloadTask* m_downloadTask;
    UploadTask* m_uploadTask;
    bool m_remove;

};

#endif /*BASE_TRANSFERTASK_H_*/
