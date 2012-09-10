/*
 *      Copyright (C) 2011-2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <map>

#include "HTTPApiHandler.h"
#include "interfaces/http-api/HttpApi.h"
#include "network/WebServer.h"

using namespace std;

bool CHTTPApiHandler::CheckHTTPRequest(const HTTPRequest &request)
{
  return ((request.method == GET || request.method == POST) && request.url.find("/xbmcCmds/xbmcHttp") == 0);
}

int CHTTPApiHandler::HandleHTTPRequest(const HTTPRequest &request)
{
  map<string, string> arguments;
  if (CWebServer::GetRequestHeaderValues(request.connection, MHD_GET_ARGUMENT_KIND, arguments) > 0)
  {
    m_responseCode = MHD_HTTP_OK;
    m_responseType = HTTPMemoryDownloadNoFreeCopy;
    CStdString command = arguments["command"];
    CStdString parameter = arguments["parameter"];
    m_response = CHttpApi::WebMethodCall(command, parameter);

    return MHD_YES;
  }

  return MHD_NO;
}
