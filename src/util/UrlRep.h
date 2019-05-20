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

#ifndef URLREP_H
#define URLREP_H

#include <string>
#include <map>

// Broken down representation of a url
struct UrlRep {
    static UrlRep fromUrl(const char* uri);
    static UrlRep fromUrl(const std::string& uri);

    UrlRep()
        : m_valid(false)
    {
    }

    virtual ~UrlRep()
    {
    }

    bool m_valid;
    std::string m_scheme;
    std::string m_userInfo;
    std::string m_host;
    std::string m_port;
    std::string m_path;
    std::string m_pathOnly;
    std::string m_resource;       //The path = pathOnly + "/" + resource
    std::map<std::string, std::string> m_query;
    std::string m_fragment;
};

#endif /* URLREP_H */
