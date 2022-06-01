/******************************************************************************
 * THIS FILE IS GENERATED - ANY EDITS WILL BE OVERWRITTEN
 */

#include "presence.h"

using namespace Quotient;

SetPresenceJob::SetPresenceJob(const QString& userId, const QString& presence,
                               const QString& statusMsg)
    : BaseJob(HttpVerb::Put, QStringLiteral("SetPresenceJob"),
              makePath("/_matrix/client/v3", "/presence/", userId, "/status"))
{
    QJsonObject _data;
    addParam<>(_data, QStringLiteral("presence"), presence);
    addParam<IfNotEmpty>(_data, QStringLiteral("status_msg"), statusMsg);
    setRequestData(std::move(_data));
}

QUrl GetPresenceJob::makeRequestUrl(QUrl baseUrl, const QString& userId)
{
    return BaseJob::makeRequestUrl(std::move(baseUrl),
                                   makePath("/_matrix/client/v3", "/presence/",
                                            userId, "/status"));
}

GetPresenceJob::GetPresenceJob(const QString& userId)
    : BaseJob(HttpVerb::Get, QStringLiteral("GetPresenceJob"),
              makePath("/_matrix/client/v3", "/presence/", userId, "/status"))
{
    addExpectedKey("presence");
}
