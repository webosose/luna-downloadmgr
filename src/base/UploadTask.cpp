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

#include <base/UploadTask.h>
#include <core/DownloadManager.h>
#include <stdlib.h>

/*
 *
 * http://wiki.developers.facebook.com/index.php/Photos.upload
 * http://www.flickr.com/services/api/upload.api.html
 *
 */

const char* UploadTask::TRUSTED_CERT_PATH = "/var/ssl/trustedcerts";

//static
UploadTask * UploadTask::newFileUploadTask(const std::string& targeturl, const std::string& sourcefile, const std::string& filemimepartlabel, std::vector<PostItem>& postparts,
        std::vector<std::string>& httpheaders, std::vector<kvpair>& cookies, const std::string& contenttype)
{
    // set up the curl handle
    CURL * p_curl = curl_easy_init();

    if (curl_easy_setopt(p_curl, CURLOPT_URL,targeturl.c_str()) != CURLE_OK) {
        curl_easy_cleanup(p_curl);
        return NULL;
    }

    /*
     * UploadTask(const std::string& url,const std::string file,const std::string& data,uint32_t id,const std::vector<kvpair> * postparts,const std::string& contenttype,CURL * p_curl); */
    UploadTask * p_ult = new UploadTask(targeturl, sourcefile, "", UploadTask::genNewId(), &postparts, contenttype, p_curl);

    // set the form info
    struct curl_httppost *last = NULL;
    for (std::vector<PostItem>::iterator it = postparts.begin(); it != postparts.end(); ++it) {
        //curl_formadd(&(p_ult->m_p_httpPostList), &last, CURLFORM_COPYNAME, it->first.c_str(), CURLFORM_COPYCONTENTS,it->second.c_str(), CURLFORM_END);

        if ((*it).m_type == ItemType_Value) {
            //this is a value part...  use CURLFORM_COPYCONTENTS
            curl_formadd(&(p_ult->m_p_httpPostList), &last, CURLFORM_COPYNAME, (*it).m_key.c_str(), CURLFORM_CONTENTTYPE, (*it).m_contentType.c_str(), CURLFORM_COPYCONTENTS, (*it).m_data.c_str(),
                    CURLFORM_END);
        } else if ((*it).m_type == ItemType_File) {
            //this is a file part... use CURLFORM_FILE
            curl_formadd(&(p_ult->m_p_httpPostList), &last, CURLFORM_COPYNAME, (*it).m_key.c_str(), CURLFORM_CONTENTTYPE, (*it).m_contentType.c_str(), CURLFORM_FILE, (*it).m_data.c_str(), CURLFORM_END);
        }
    }

    //if there is a content type specified, prepare curl headers especially for it
    if (contenttype.size()) {

        std::string ctheader = std::string("Content-Type: ") + contenttype;
        p_ult->m_p_curlFilePartHeaderList = curl_slist_append(p_ult->m_p_curlFilePartHeaderList, ctheader.c_str());
    }

    if (contenttype.size())
        curl_formadd(&(p_ult->m_p_httpPostList), &last, CURLFORM_COPYNAME, filemimepartlabel.c_str()
//              , CURLFORM_CONTENTHEADER, p_ult->m_p_curlFilePartHeaderList
                , CURLFORM_FILE, sourcefile.c_str(), CURLFORM_CONTENTTYPE, p_ult->m_contentType.c_str(), CURLFORM_END);
    else
        curl_formadd(&(p_ult->m_p_httpPostList), &last, CURLFORM_COPYNAME, filemimepartlabel.c_str(), CURLFORM_FILE, sourcefile.c_str(), CURLFORM_END);

    curl_easy_setopt(p_curl, CURLOPT_HTTPPOST, (p_ult->m_p_httpPostList));

    // set http headers
    p_ult->setHTTPHeaders(httpheaders);

    //set all the cookies
    // curl_easy_setopt(easyhandle, CURLOPT_COOKIE, "name1=var1; name2=var2;");
    std::string cookiestr;
    for (std::vector<kvpair>::iterator it = cookies.begin(); it != cookies.end(); ++it) {
        cookiestr += it->first + std::string("=") + it->second + std::string("; ");
    }
    curl_easy_setopt(p_curl, CURLOPT_COOKIE, cookiestr.c_str());
    curl_easy_setopt(p_curl, CURLOPT_CAPATH, TRUSTED_CERT_PATH);
    curl_easy_setopt(p_curl, CURLOPT_WRITEFUNCTION, DownloadManager::cbUploadResponse);
    curl_easy_setopt(p_curl, CURLOPT_WRITEDATA, p_ult);
    curl_easy_setopt(p_curl, CURLOPT_WRITEHEADER, p_curl);
    curl_easy_setopt(p_curl, CURLOPT_HEADERFUNCTION, DownloadManager::cbCurlHeaderInfo);
    // curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    return p_ult;
}

UploadTask * UploadTask::newBufferUploadTask(const std::string& targeturl, const std::string& sourcebuffer, std::vector<std::string>& httpheaders, const std::string& contenttype)
{
    //TODO: check parameter validity
    // LOG_DEBUG ("%s: ", __func__);

    // set up the curl handle
    CURL * p_curl = curl_easy_init();

    if (curl_easy_setopt(p_curl, CURLOPT_URL,targeturl.c_str()) != CURLE_OK) {
        curl_easy_cleanup(p_curl);
        return NULL;
    }

    // LOG_DEBUG ("%s: url %s", __func__, targeturl.c_str());
    /*
     * UploadTask(const std::string& url,const std::string file,const std::string& data,uint32_t id,const std::vector<kvpair> * postparts,const std::string& contenttype,CURL * p_curl); */
    UploadTask * p_ult = new UploadTask(targeturl, sourcebuffer, "", UploadTask::genNewId(), NULL, contenttype, p_curl);

    curl_easy_setopt(p_ult->m_p_curlHandle, CURLOPT_POSTFIELDS, sourcebuffer.c_str());
    curl_easy_setopt(p_ult->m_p_curlHandle, CURLOPT_POSTFIELDSIZE, sourcebuffer.length());
    // LOG_DEBUG ("%s: sending postfield %s with postfieldsize %d", __func__, sourcebuffer.c_str(), sourcebuffer.length());

    // curl_easy_setopt(p_ult->m_p_curlHandle, CURLOPT_HTTPPOST, (p_ult->m_p_httpPostList));

    // set http headers
    p_ult->setHTTPHeaders(httpheaders);

    curl_easy_setopt(p_curl, CURLOPT_CAPATH, TRUSTED_CERT_PATH);
    curl_easy_setopt(p_curl, CURLOPT_WRITEFUNCTION, DownloadManager::cbUploadResponse);
    curl_easy_setopt(p_curl, CURLOPT_WRITEDATA, p_ult);
    curl_easy_setopt(p_curl, CURLOPT_WRITEHEADER, p_curl);
    curl_easy_setopt(p_curl, CURLOPT_HEADERFUNCTION, DownloadManager::cbCurlHeaderInfo);
    // curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    return p_ult;
}
UploadTask::UploadTask(const std::string& url, const std::string file, const std::string& data, uint32_t id, std::vector<PostItem> * postparts, const std::string& contenttype, CURL * p_curl) :
        m_url(url), m_sourceFile(file), m_sourceData(data), m_ulid(id), m_contentType(contenttype), m_p_curlHandle(p_curl), m_p_curlHeaderList(0), m_p_curlFilePartHeaderList(0), m_p_httpPostList(0), m_curlResultCode(
                CURLE_OK), m_httpResultCode(0)
{
    if (postparts)
        m_postParts = *postparts;
}

void UploadTask::setHTTPHeaders(std::vector<std::string>& headerList)
{
    if (m_p_curlHandle == NULL)
        return;

    //clear existing headers
    if (m_p_curlHeaderList) {
        curl_slist_free_all(m_p_curlHeaderList);
        m_p_curlHeaderList = 0;
    }

    for (std::vector<std::string>::iterator it = headerList.begin(); it != headerList.end(); ++it)
        m_p_curlHeaderList = curl_slist_append(m_p_curlHeaderList, (*it).c_str());

    curl_easy_setopt(m_p_curlHandle, CURLOPT_HTTPHEADER, m_p_curlHeaderList);

}

UploadTask::~UploadTask()
{
    if (m_p_curlHeaderList) {
        curl_slist_free_all(m_p_curlHeaderList);
    }
    if (m_p_httpPostList) {
        curl_formfree(m_p_httpPostList);
    }
    if (m_p_curlFilePartHeaderList) {
        curl_slist_free_all(m_p_curlFilePartHeaderList);
    }

    curl_easy_cleanup(m_p_curlHandle);
}

//TODO: moved here temporarily to hack in cancel support for uploads. Move back to it's logical location
uint32_t UploadTask::genNewId()
{
    return (DownloadManager::instance().generateNewTicket());
}

