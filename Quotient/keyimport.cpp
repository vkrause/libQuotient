// SPDX-FileCopyrightText: 2024 Tobias Fella <tobias.fella@kde.org>
// SPDX-License-Identifier: LGPL-2.0-or-later

#include "keyimport.h"

#include <QtEndian>
#include <QDebug>

#include "e2ee/cryptoutils.h"
#include "connection.h"
#include "room.h"

#include "logging_categories_p.h"

using namespace Quotient;

const auto VersionLength = 1;
const auto SaltOffset = VersionLength;
const auto IvOffset = SaltOffset + AesBlockSize;
const auto RoundsOffset = IvOffset + AesBlockSize;
const auto RoundsLength = 4;
const auto PayloadOffset = RoundsOffset + RoundsLength;
const auto MacLength = 32;
const auto HeaderLength = VersionLength + AesBlockSize + AesBlockSize + RoundsLength + MacLength;

Expected<QJsonArray, KeyImport::Error> KeyImport::decrypt(QString data, const QString& passphrase)
{
    data.remove("-----BEGIN MEGOLM SESSION DATA-----"_ls);
    data.remove("-----END MEGOLM SESSION DATA-----"_ls);
    data.remove(u'\n');
    auto decoded = QByteArray::fromBase64(data.toLatin1());
    if (decoded[0] != 1) {
        qCWarning(E2EE) << "Wrong version byte";
        return InvalidData;
    }

    if (decoded.size() < HeaderLength) {
        qCWarning(E2EE) << "Data not long enough";
        return InvalidData;
    }

    const auto salt = decoded.mid(SaltOffset, AesBlockSize);
    const auto iv = decoded.mid(IvOffset, AesBlockSize);
    const auto rounds = qFromBigEndian<uint32_t>(decoded.mid(RoundsOffset, RoundsLength).data());
    const auto payload = decoded.mid(PayloadOffset, decoded.size() - HeaderLength);
    const auto expectedMac = decoded.right(MacLength);

    auto keys = pbkdf2HmacSha512<64>(passphrase.toLatin1(), salt, rounds);
    if (!keys.has_value()) {
        qCWarning(E2EE) << "Failed to calculate pbkdf:" << keys.error();
        return OtherError;
    }

    auto actualMac = hmacSha256(key_view_t(keys.value().begin() + 32, 32), decoded.left(decoded.size() - MacLength));
    if (!actualMac.has_value()) {
        qCWarning(E2EE) << "Failed to calculate hmac:" << actualMac.error();
        return OtherError;
    }

    if (actualMac.value() != expectedMac) {
        qCWarning(E2EE) << "Mac incorrect";
        return InvalidPassphrase;
    }

    auto plain = aesCtr256Decrypt(payload, byte_view_t<Aes256KeySize>(keys.value().begin(), Aes256KeySize), asCBytes<AesBlockSize>(iv));
    if (!plain.has_value()) {
        qCWarning(E2EE) << "Failed to decrypt data";
        return OtherError;
    }
    return QJsonDocument::fromJson(plain.value()).array();
}

KeyImport::Error KeyImport::importKeys(QString data, const QString& passphrase, const Connection* connection)
{
    auto result = decrypt(std::move(data), passphrase);
    if (!result.has_value()) {
        return result.error();
    }

    for (const auto& key : result.value()) {
        const auto& keyObject = key.toObject();
        const auto& room = connection->room(keyObject[RoomIdKey].toString());
        if (!room) {
            continue;
        }
        // We don't know the session index for these sessions here. We just pretend it's 0, it's not terribly important.
        room->addMegolmSessionFromBackup(
            keyObject["session_id"_ls].toString().toLatin1(),
            keyObject["session_key"_ls].toString().toLatin1(), 0,
            keyObject[SenderKeyKey].toVariant().toByteArray());
    }
    return Success;
}

