// SPDX-FileCopyrightText: 2023 Tobias Fella <tobias.fella@kde.org>
// SPDX-License-Identifier: LGPL-2.0-or-later

#pragma once

#include "e2ee_common.h"
#include "expected.h"

#include "../quotient_export.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace Quotient {

// Common remark: OpenSSL is a private dependency of libQuotient, meaning that
// headers of libQuotient can't include OpenSSL headers. Instead, alias
// the value or type along with a comment (see SslErrorCode e.g.) and add
// static_assert in the .cpp file to check it against the OpenSSL definition.

constexpr auto DefaultPbkdf2KeyLength = 32u;
constexpr auto Aes256KeySize = 32u;
constexpr auto AesBlockSize = 16u; // AES_BLOCK_SIZE
constexpr auto HmacKeySize = 32u;

struct QUOTIENT_API HkdfKeys
    : public FixedBuffer<Aes256KeySize + HmacKeySize> {
    //! \brief Key to be used for AES encryption / decryption
    auto aes() const { return asCBytes(*this).first<Aes256KeySize>(); }
    //! \brief Key to be used for MAC creation / verification
    auto mac() const { return asCBytes(*this).last<HmacKeySize>(); }
};

struct QUOTIENT_API Curve25519Encrypted {
    QByteArray ciphertext;
    QByteArray mac;
    QByteArray ephemeral;
};

// NOLINTNEXTLINE(google-runtime-int): the type is copied from OpenSSL
using SslErrorCode = unsigned long; // decltype(ERR_get_error())

enum SslErrorCodes : SslErrorCode {
    SslErrorUserOffset = 128, // ERR_LIB_USER; never use this bare
    WrongDerivedKeyLength = SslErrorUserOffset + 1,
    SslPayloadTooLong = SslErrorUserOffset + 2
};

//! Same as QOlmExpected but for wrapping OpenSSL instead of Olm calls
template <typename T>
using SslExpected = Expected<T, SslErrorCode>;

inline QByteArray zeroedByteArray(QByteArray::size_type n = 32)
{
    return { n, '\0' };
}

//! Generate a key out of the given password
QUOTIENT_API SslExpected<QByteArray> pbkdf2HmacSha512(
    const QByteArray& password, const QByteArray& salt, int iterations,
    int keyLength = DefaultPbkdf2KeyLength);

//! \brief Derive a key from the input data using HKDF-SHA256
//!
//! All input parameters have a length of 32 bytes
QUOTIENT_API SslExpected<HkdfKeys> hkdfSha256(const QByteArray& key,
                                              const QByteArray& salt,
                                              const QByteArray& info);

//! Calculate a MAC from the given key and data
QUOTIENT_API SslExpected<QByteArray> hmacSha256(
    byte_view_t<HmacKeySize> hmacKey,
    const QByteArray& data);

//! \brief Decrypt the data using Curve25519-AES-Sha256
//! \note ciphertext must be given as base64
QUOTIENT_API QOlmExpected<QByteArray> curve25519AesSha2Decrypt(
    QByteArray ciphertext, const QByteArray& privateKey,
    const QByteArray& ephemeral, const QByteArray& mac);

//! \brief Encrypt the data using Curve25519-AES-Sha256
//! \note publicKey must be given as base64
QUOTIENT_API QOlmExpected<Curve25519Encrypted> curve25519AesSha2Encrypt(
    const QByteArray& plaintext, const QByteArray& publicKey);

//! \brief Encrypt data using AES-CTR-256
//!
//! key and iv have a length of 32 bytes
QUOTIENT_API SslExpected<QByteArray> aesCtr256Encrypt(
    const QByteArray& plaintext, byte_view_t<Aes256KeySize> key,
    byte_view_t<AesBlockSize> iv);

//! \brief Decrypt data using AES-CTR-256
//!
//! key and iv have a length of 32 bytes
QUOTIENT_API SslExpected<QByteArray> aesCtr256Decrypt(
    const QByteArray& ciphertext, byte_view_t<Aes256KeySize> key,
    byte_view_t<AesBlockSize> iv);

QUOTIENT_API QByteArray base58Decode(const QByteArray& encoded);

} // namespace Quotient
