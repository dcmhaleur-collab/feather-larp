// SPDX-License-Identifier: BSD-3-Clause

#ifndef FEATHER_SPOOFMODE_H
#define FEATHER_SPOOFMODE_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QTimer>

#include "libwalletqt/rows/TransactionRow.h"

class Wallet;
class SpoofBridge;

class SpoofMode : public QObject
{
    Q_OBJECT

public:
    static SpoofMode *instance();
    static quint64 feeAtomic();

    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Cross-wallet bridge: link this Feather instance to a peer larp wallet
    // (e.g. the Monero GUI) so balances/sends/receives can be mirrored across
    // the two running exes. Safe to call every time a wallet is opened.
    void attachWallet(Wallet *wallet);
    void detachWallet();
    void registerAddresses();
    bool isBridgeConnected() const;

    bool hasBalance(quint32 accountIndex) const;
    quint64 balance(quint32 accountIndex, quint64 fallback) const;
    void setBalance(quint32 accountIndex, quint64 atomicUnits);
    void replaceBalances(const QHash<quint32, quint64> &balances);
    void clearBalance(quint32 accountIndex);
    void clearAll();

    QList<TransactionRow> transactions(quint32 accountIndex) const;

    // Local-only visible simulator. This never constructs, signs, broadcasts,
    // or proves a real blockchain transaction.
    bool simulateSend(Wallet *wallet,
                      const QVector<QString> &addresses,
                      const QVector<quint64> &amounts,
                      const QString &description,
                      QString *error);

signals:
    void changed();

private slots:
    void advanceConfirmations();
    void onRemoteSpoofedTx(const QString &txid, quint64 amount, const QString &toAddress,
                           const QString &description, qint64 timestamp);

private:
    explicit SpoofMode(QObject *parent = nullptr);

    QString makeSpoofTxId() const;

    bool m_enabled = false;
    QHash<quint32, quint64> m_balances;
    QHash<quint32, QList<TransactionRow>> m_transactions;
    QTimer m_confirmationTimer;

    SpoofBridge *m_bridge = nullptr;
    QPointer<Wallet> m_wallet;
};

#endif // FEATHER_SPOOFMODE_H
