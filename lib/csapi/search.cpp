/******************************************************************************
 * THIS FILE IS GENERATED - ANY EDITS WILL BE OVERWRITTEN
 */

#include "search.h"

#include <QtCore/QStringBuilder>

using namespace Quotient;

auto queryToSearch(const QString& nextBatch)
{
    BaseJob::Query _q;
    addParam<IfNotEmpty>(_q, QStringLiteral("next_batch"), nextBatch);
    return _q;
}

SearchJob::SearchJob(const Categories& searchCategories,
                     const QString& nextBatch)
    : BaseJob(HttpVerb::Post, QStringLiteral("SearchJob"),
              QStringLiteral("/_matrix/client/r0") % "/search",
              queryToSearch(nextBatch))
{
    QJsonObject _data;
    addParam<>(_data, QStringLiteral("search_categories"), searchCategories);
    setRequestData(std::move(_data));
    addExpectedKey("search_categories");
}
