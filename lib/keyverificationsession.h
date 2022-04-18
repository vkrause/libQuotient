// SPDX-FileCopyrightText: 2022 Tobias Fella <fella@posteo.de>
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "events/keyverificationevent.h"
#include <QtCore/QObject>
#include <qchar.h>

class OlmSAS;

namespace Quotient {
class Connection;

/** A key verification session. Listen for incoming sessions by connecting to Connection::newKeyVerificationSession.
    Start a new session using Connection::startKeyVerificationSession.
    The object is delete after finished is emitted.
*/
class QUOTIENT_API KeyVerificationSession : public QObject
{
    Q_OBJECT

public:
    enum State {
        INCOMING, // There is a request for verification incoming
        WAITINGFORREADY, // We sent a request for verification and are waiting for ready
        READY, // Either party sent a ready as a response to a request; The user selects a method
        WAITINGFORACCEPT, // We sent a start and are waiting for an accept
        ACCEPTED, // The other party sent an accept and is waiting for a key
        WAITINGFORKEY, // We're waiting for a key
        WAITINGFORVERIFICATION, // We're waiting for the *user* to verify the emojis
        WAITINGFORMAC, // We're waiting for the mac
        CANCELED, // The session has been canceled
        DONE, // The verification is done
    };
    Q_ENUM(State)

    enum Error {
        NONE,
        TIMEOUT,
        REMOTE_TIMEOUT,
        USER,
        REMOTE_USER,
        UNEXPECTED_MESSAGE,
        REMOTE_UNEXPECTED_MESSAGE,
        UNKNOWN_TRANSACTION,
        REMOTE_UNKNOWN_TRANSACTION,
        UNKNOWN_METHOD,
        REMOTE_UNKNOWN_METHOD,
        KEY_MISMATCH,
        REMOTE_KEY_MISMATCH,
        USER_MISMATCH,
        REMOTE_USER_MISMATCH,
        INVALID_MESSAGE,
        REMOTE_INVALID_MESSAGE,
        SESSION_ACCEPTED,
        REMOTE_SESSION_ACCEPTED,
        MISMATCHED_COMMITMENT,
        REMOTE_MISMATCHED_COMMITMENT,
        MISMATCHED_SAS,
        REMOTE_MISMATCHED_SAS,
    };
    Q_ENUM(Error);

    //Q_PROPERTY(int timeLeft READ timeLeft NOTIFY timeLeftChanged)
    Q_PROPERTY(QString remoteDeviceId MEMBER m_remoteDeviceId CONSTANT)
    Q_PROPERTY(QList<QVariantMap> sasEmojis READ sasEmojis NOTIFY sasEmojisChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(Error error READ error NOTIFY errorChanged)

    KeyVerificationSession(const QString& remoteUserId, const KeyVerificationRequestEvent& event, Connection* connection, bool encrypted, QObject* parent = nullptr);
    KeyVerificationSession(const QString& userId, const QString& deviceId, Connection* connection, QObject* parent = nullptr);
    ~KeyVerificationSession();

    int timeLeft() const;
    QList<QVariantMap> sasEmojis() const;
    State state() const;

    Error error() const;

public Q_SLOTS:
    void sendRequest();
    void sendReady();
    void sendMac();
    void sendStartSas();
    void sendKey();
    void sendDone();
    void cancelVerification(Error error);

Q_SIGNALS:

    void timeLeftChanged();
    void startReceived();
    void keyReceived();
    void sasEmojisChanged();
    void stateChanged();
    void errorChanged();
    void finished();

private:
    QString m_remoteUserId;
    QString m_remoteDeviceId;
    QString m_transactionId;
    Connection* m_connection;
    OlmSAS* m_sas = nullptr;
    int timeleft = 0;
    QList<QVariantMap> m_sasEmojis;
    bool startSentByUs = false;
    State m_state;
    Error m_error;
    QString m_startEvent;
    QString m_commitment;
    QString m_language;
    int m_timeout;
    bool macReceived = false;
    bool m_encrypted;
    QStringList m_remoteSupportedMethods;

    void handleReady(const KeyVerificationReadyEvent& event);
    void handleStart(const KeyVerificationStartEvent& event);
    void handleAccept(const KeyVerificationAcceptEvent& event);
    void handleKey(const KeyVerificationKeyEvent& event);
    void handleMac(const KeyVerificationMacEvent& event);
    void handleDone(const KeyVerificationDoneEvent& event);
    void handleCancel(const KeyVerificationCancelEvent& event);
    void init();
    void setState(State state);
    void setError(Error error);
    QStringList commonSupportedMethods(const QStringList& remoteSupportedMethods) const;
    QString errorToString(Error error) const;
    Error stringToError(const QString& error) const;
    QStringList m_supportedMethods = { "m.sas.v1"_ls };

    QByteArray macInfo(bool verifying, const QString& key = "KEY_IDS"_ls);
    QString calculateMac(const QString& input, bool verifying, const QString& keyId= "KEY_IDS"_ls);

    std::pair<QString, QString> emojiForCode(int code);
};

}
