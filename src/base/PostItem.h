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

#ifndef BASE_POSTITEM_H_
#define BASE_POSTITEM_H_

#include <iostream>

enum ItemType {
    File,
    Value,
    Buffer
};

/*
 * no checking on values...container class only
 */
class PostItem {
public:
    PostItem(const std::string& key, const std::string& data, ItemType type, const std::string& contentType)
        : m_key(key),
          m_type(type),
          m_data(data),
          m_contentType(contentType)
    {
    }

    PostItem(const PostItem& c)
    {
        m_key = c.m_key;
        m_type = c.m_type;
        m_data = c.m_data;
        m_contentType = c.m_contentType;
    }

    PostItem& operator=(const PostItem& c)
    {
        if (&c == this)
            return *this;
        m_key = c.m_key;
        m_type = c.m_type;
        m_data = c.m_data;
        m_contentType = c.m_contentType;
        return *this;
    }

    std::string m_key;
    ItemType m_type;
    std::string m_data;
    std::string m_contentType;
};

#endif /* BASE_POSTITEM_H_ */
