// SPDX-FileCopyrightText: 2023 Tobias Fella <tobias.fella@kde.org>
// SPDX-License-Identifier: LGPL-2.0-or-later

#include "sssshandler.h"

#include "e2ee/cryptoutils.h"
#include "room.h"
#include "../logging_categories_p.h"
#include "../qt_connection_util.h"

using namespace Quotient;

SSSSHandler::SSSSHandler(QObject* parent)
    : QObject(parent)
{}

namespace Quotient {
class QUOTIENT_API GetRoomKeysVersionJob : public BaseJob {
public:
    using BaseJob::makeRequestUrl;
    static QUrl makeRequestUrl(QUrl baseUrl, const QUrl& mxcUri,
                               QSize requestedSize);

    GetRoomKeysVersionJob()
        : BaseJob(HttpVerb::Get, {},
              "/_matrix/client/v3/room_keys/version")
    {}
};

class QUOTIENT_API GetRoomKeysJob : public BaseJob {
public:
    using BaseJob::makeRequestUrl;
    static QUrl makeRequestUrl(QUrl baseUrl, const QUrl& mxcUri,
                               QSize requestedSize);

    GetRoomKeysJob(const QString& version)
        : BaseJob(HttpVerb::Get, {},
              "/_matrix/client/v3/room_keys/keys")
    {
        QUrlQuery query;
        addParam<>(query, QStringLiteral("version"), version);
        setRequestQuery(query);
    }
};
} // namespace Quotient

QByteArray SSSSHandler::decryptKey(const QString& name, const QByteArray& decryptionKey) const
{
    Q_ASSERT(m_connection);
    const auto defaultKey = m_connection->accountData("m.secret_storage.default_key"_ls)->contentPart<QString>("key"_ls);
    const auto& encrypted = m_connection->accountData(name)->contentPart<QJsonObject>("encrypted"_ls)[defaultKey];

    auto keys = hkdfSha256(decryptionKey, QByteArray(32, u'\0'), QByteArrayLiteral("m.megolm_backup.v1"));

    auto rawCipher = QByteArray::fromBase64(encrypted["ciphertext"_ls].toString().toLatin1());
    if (QString::fromLatin1(hmacSha256(keys.mac, rawCipher).toBase64()) != encrypted["mac"_ls].toString()) {
        qWarning() << "MAC mismatch for" << name;
        return {};
    }
    return QByteArray::fromBase64(aesCtr256Decrypt(rawCipher, keys.aes, QByteArray::fromBase64(encrypted["iv"_ls].toString().toLatin1())));

}

void SSSSHandler::unlockSSSSFromPassword(const QString& password)
{
    Q_ASSERT(m_connection);
    calculateDefaultKey(password.toLatin1(), true);
}

void SSSSHandler::unlockSSSSFromCrossSigning()
{
    Q_ASSERT(m_connection);
    Connection::UsersToDevicesToContent content;
    const auto& requestId = m_connection->generateTxnId();
    QJsonObject eventContent;
    eventContent["action"_ls] = "request"_ls;
    eventContent["name"_ls] = "m.megolm_backup.v1"_ls;
    eventContent["request_id"_ls] = requestId;
    eventContent["requesting_device_id"_ls] = m_connection->deviceId();
    for (const auto& deviceId : m_connection->devicesForUser(m_connection->userId())) {
        content[m_connection->userId()][deviceId] = eventContent;
    }
    m_connection->sendToDevices("m.secret.request"_ls, content);
    connectUntil(m_connection.data(), &Connection::secretReceived, this, [this, requestId](const QString& receivedRequestId, const QString& secret) {
        if (requestId != receivedRequestId) {
            return false;
        }
        const auto& megolmDecryptionKey = QByteArray::fromBase64(secret.toLatin1());
        loadMegolmBackup(megolmDecryptionKey);
        return true;
    });
}

Connection* SSSSHandler::connection() const
{
    return m_connection;
}

void SSSSHandler::setConnection(Connection* connection)
{
    if (connection == m_connection) {
        return;
    }
    m_connection = connection;
    Q_EMIT connectionChanged();
}

void SSSSHandler::loadMegolmBackup(const QByteArray& megolmDecryptionKey)
{
    auto job = m_connection->callApi<GetRoomKeysVersionJob>();
    connect(job, &BaseJob::finished, this, [this, job, megolmDecryptionKey](){
        auto keysJob = m_connection->callApi<GetRoomKeysJob>(job->jsonData()["version"_ls].toString());
        connect(keysJob, &BaseJob::finished, this, [this, keysJob, megolmDecryptionKey](){
            const auto &rooms = keysJob->jsonData()["rooms"_ls].toObject();
            for (const auto& roomId : rooms.keys()) {
                if (!m_connection->room(roomId)) {
                    continue;
                }
                const auto &sessions = rooms[roomId]["sessions"_ls].toObject();
                for (const auto& sessionId : sessions.keys()) {
                    const auto &session = sessions[sessionId].toObject();
                    const auto &sessionData = session["session_data"_ls].toObject();
                    auto data = QJsonDocument::fromJson(curve25519AesSha2Decrypt(sessionData["ciphertext"_ls].toString().toLatin1(), megolmDecryptionKey, sessionData["ephemeral"_ls].toString().toLatin1(), sessionData["mac"_ls].toString().toLatin1())).object();
                    m_connection->room(roomId)->addMegolmSessionFromBackup(sessionId.toLatin1(), data["session_key"_ls].toString().toLatin1(), session["first_message_index"_ls].toInt());
                }
            }
        });
    });
}

QByteArray decode_base58(const QByteArray& input)
{
    auto alphabet = QByteArrayLiteral("123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
    QByteArray reverse_alphabet(256, -1);
    for (auto i = 0; i < 58; i++) {
        reverse_alphabet[static_cast<uint8_t>(alphabet.at(i))] = static_cast<char>(i);
    }

    QByteArray result;
    result.reserve(input.size() * 733 / 1000 + 1);

    for (auto b : input) {
        uint32_t carry = reverse_alphabet[b];
        for (auto &j : result) {
            carry += static_cast<uint8_t>(j) * 58;
            j = static_cast<char>(static_cast<uint8_t>(carry % 0x100));
            carry /= 0x100;
        }
        while (carry > 0) {
            result.push_back(static_cast<char>(static_cast<uint8_t>(carry % 0x100)));
            carry /= 0x100;
        }
    }

    for (auto i = 0; i < input.length() && input[i] == '1'; i++) {
        result.push_back(u'\0');
    }

    std::reverse(result.begin(), result.end());
    return result;
}

void SSSSHandler::calculateDefaultKey(const QByteArray& secret, bool passphrase)
{
    auto key = secret;

    const auto defaultKey = m_connection->accountData("m.secret_storage.default_key"_ls)->contentPart<QString>("key"_ls);
    const auto &keyEvent = m_connection->accountData("m.secret_storage.key."_ls + defaultKey);
    if (keyEvent->contentPart<QString>("algorithm"_ls) != "m.secret_storage.v1.aes-hmac-sha2"_ls) {
        qCWarning(E2EE) << "Unsupported SSSS key algorithm" << keyEvent->contentPart<QString>("algorithm"_ls) << " - aborting.";
        return;
    }

    if (passphrase) {
        const auto &passphraseJson = keyEvent->contentPart<QJsonObject>("passphrase"_ls);
        if (passphraseJson["algorithm"_ls].toString() != "m.pbkdf2"_ls) {
            qCWarning(E2EE) << "Unsupported SSSS passphrase algorithm" << passphraseJson["algorithm"_ls].toString() << " - aborting.";
            return;
        }

        key = pbkdf2HmacSha512(secret, passphraseJson["salt"_ls].toString().toLatin1(), passphraseJson["iterations"_ls].toInt(), 32);
    }


    const auto &testKeys = hkdfSha256(key, QByteArray(32, u'\0'), {});
    const auto &encrypted = aesCtr256Encrypt(QByteArray(32, u'\0'), testKeys.aes, QByteArray::fromBase64(keyEvent->contentPart<QString>("iv"_ls).toLatin1()));
    if (hmacSha256(testKeys.mac, encrypted) != QByteArray::fromBase64(keyEvent->contentPart<QString>("mac"_ls).toLatin1())) {
        qCWarning(E2EE) << "MAC mismatch for secret storage key";
        emit keyBackupPasswordWrong();
        return;
    }

    emit keyBackupPasswordCorrect();

    auto megolmDecryptionKey = decryptKey("m.megolm_backup.v1"_ls, key);
    if (megolmDecryptionKey.isEmpty()) {
        return;
    }
    loadMegolmBackup(megolmDecryptionKey);
}

void SSSSHandler::unlockSSSSFromSecurityKey(const QString& key)
{
    auto securityKey = key;
    securityKey.remove(u' ');
    auto decoded = decode_base58(securityKey.toLatin1());
    unsigned char parity = 0;
    for (const auto b : decoded) {
        parity ^= b;
    }
    if (parity != 0) {
        qCWarning(E2EE) << "Invalid parity byte";
        return;
    }
    if (static_cast<uint8_t>(decoded[0]) != 0x8B || static_cast<uint8_t>(decoded[1]) != 0x01) {
        qCWarning(E2EE) << "Invalid prefix";
        return;
    }
    if (decoded.length() != 32 + 3) {
        qCWarning(E2EE) << "Incorrect length";
        return;
    }
    decoded = decoded.mid(2);
    decoded.chop(1);

    calculateDefaultKey(decoded, false);
}
