// Copyright (c) 2013-2019 LG Electronics, Inc.
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

#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <glib.h>

//! List of utilites for common
class Utils {
private:
    // abstract class for async call
    class IAsyncCall {
    public:
        virtual ~IAsyncCall()
        {
        }
        virtual void Call() = 0;
    };

    // implementaion for async call
    template<typename T>
    class AsyncCall: public IAsyncCall {
    public:
        AsyncCall(T _func) :
                func(_func)
        {
        }

        void Call()
        {
            func();
        }
    private:
        T func;
    };

public:
    //! Read file contents
    static std::string read_file(const std::string &path);

    //! Make directory
    static bool make_dir(const std::string &path, bool withParent = true);

    //! Remove directory recursive
    static bool remove_dir(const std::string &path);

    //! Remove file
    static bool remove_file(const std::string &path);

    //! Make std::string for type T
    template<class T>
    static std::string toString(const T &arg)
    {
        std::ostringstream out;
        out << arg;
        return (out.str());
    }

    //! Call function asynchronously
    template<typename T>
    static bool async(T function)
    {
        AsyncCall<T> *p = new AsyncCall<T>(function);
        g_timeout_add(0, cbAsync, (gpointer) p);
        return true;
    }

private:

    //! It's called when get response async call
    static gboolean cbAsync(gpointer data);
};

#endif
