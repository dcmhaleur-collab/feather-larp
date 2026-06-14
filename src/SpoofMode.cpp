// SPDX-License-Identifier: BSD-3-Clause

#include "SpoofMode.h"

#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

#include "constants.h"
#include "SpoofBridge.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/rows/Output.h"

namespace {
    constexpr quint64 kSpoofFeeAtomic = 200000000; // 0.0002 XMR
    constexpr int kConfirmationIntervalMs = 15000;
    constexpr quint64 kMaxSpoofConfirmations = 6;

    // Feather mirrors the Monero GUI SpoofBridge: it listens on 48084 and
    // connects out to 48083 (the Monero GUI does the inverse). Both run on
    // localhost so launching the two exes is enough to link them.
    constexpr quint16 kBridgeListenPort = 48084;
    constexpr quint16 kBridgeRemotePort = 48083;
}

SpoofMode *SpoofMode::instance()
{
    static auto *mode = new SpoofMode();
    return mode;
}

quint64 SpoofMode::feeAtomic()
{
    return kSpoofFeeAtomic;
}

SpoofMode::SpoofMode(QObject *parent)
    : QObject(parent)
{
    m_confirmationTimer.setInterval(kConfirmationIntervalMs);
    connect(&m_confirmationTimer, &QTimer::timeout, this, &SpoofMode::advanceConfirmations);
}

void SpoofMode::attachWallet(Wallet *wallet)
{
    m_wallet = wallet;

    if (!m_bridge) {
        m_bridge = new SpoofBridge("feather", kBridgeListenPort, "127.0.0.1", kBridgeRemotePort, this);

        connect(m_bridge, &SpoofBridge::spoofedTxReceived, this, &SpoofMode::onRemoteSpoofedTx);
        connect(m_bridge, &SpoofBridge::logMessage, this, [](const QString &msg) {
            qDebug() << msg;
        });
    }

    registerAddresses();
}

void SpoofMode::detachWallet()
{
    m_wallet = nullptr;
}

void SpoofMode::registerAddresses()
{
    if (!m_bridge || !m_wallet) {
        return;
    }

    QStringList addrs;
    const quint32 numAccounts = m_wallet->numSubaddressAccounts();
    for (quint32 a = 0; a < numAccounts; ++a) {
        const quint32 numSub = m_wallet->numSubaddresses(a);
        for (quint32 s = 0; s < numSub; ++s) {
            addrs.append(m_wallet->address(a, s));
        }
    }
    m_bridge->setLocalAddresses(addrs);
}

bool SpoofMode::isBridgeConnected() const
{
    return m_bridge && (m_bridge->isConnected() || m_bridge->isRemoteConnected());
}

void SpoofMode::onRemoteSpoofedTx(const QString &txid, quint64 amount, const QString &toAddress,
                                  const QString &description, qint64 timestamp)
{
    if (!m_wallet) {
        return;
    }

    const SubaddressIndex idx = m_wallet->subaddressIndex(toAddress);
    if (!idx.isValid()) {
        return; // not one of our addresses
    }

    const quint32 toAccount = static_cast<quint32>(idx.major);
    const quint64 current = balance(toAccount, m_wallet->unlockedBalance(toAccount));
    m_balances[toAccount] = current + amount;

    TransactionRow incoming;
    incoming.amount = static_cast<qint64>(amount);
    incoming.balanceDelta = static_cast<qint64>(amount);
    incoming.fee = 0;
    incoming.direction = TransactionRow::Direction_In;
    incoming.hash = txid.isEmpty() ? makeSpoofTxId() : txid;
    incoming.description = description.isEmpty() ? QStringLiteral("Transfer") : description;
    incoming.subaddrAccount = toAccount;
    incoming.subaddrIndex.insert(static_cast<quint32>(idx.minor));
    incoming.timestamp = timestamp > 0 ? QDateTime::fromMSecsSinceEpoch(timestamp)
                                       : QDateTime::currentDateTime();
    incoming.pending = true;
    incoming.confirmations = 0;
    incoming.unlockTime = kMaxSpoofConfirmations;
    incoming.transfers.emplace_back(amount, toAddress);

    m_transactions[toAccount].prepend(incoming);

    // Receiving larp funds implies the user is in a larp session: make sure the
    // simulation is visible even if spoof mode was toggled off locally.
    if (!m_enabled) {
        setEnabled(true);
        return; // setEnabled already emits changed()
    }

    if (!m_confirmationTimer.isActive()) {
        m_confirmationTimer.start();
    }

    emit changed();
}

bool SpoofMode::isEnabled() const
{
    return m_enabled;
}

void SpoofMode::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;

    if (m_enabled && !m_confirmationTimer.isActive()) {
        m_confirmationTimer.start();
    } else if (!m_enabled) {
        m_confirmationTimer.stop();
    }

    emit changed();
}

bool SpoofMode::hasBalance(quint32 accountIndex) const
{
    return m_balances.contains(accountIndex);
}

quint64 SpoofMode::balance(quint32 accountIndex, quint64 fallback) const
{
    if (!m_enabled || !m_balances.contains(accountIndex)) {
        return fallback;
    }
    return m_balances.value(accountIndex);
}

void SpoofMode::setBalance(quint32 accountIndex, quint64 atomicUnits)
{
    if (m_balances.value(accountIndex) == atomicUnits && m_balances.contains(accountIndex)) {
        return;
    }

    m_balances[accountIndex] = atomicUnits;
    emit changed();
}

void SpoofMode::replaceBalances(const QHash<quint32, quint64> &balances)
{
    if (m_balances == balances) {
        return;
    }

    m_balances = balances;
    emit changed();
}

void SpoofMode::clearBalance(quint32 accountIndex)
{
    if (!m_balances.contains(accountIndex)) {
        return;
    }

    m_balances.remove(accountIndex);
    emit changed();
}

void SpoofMode::clearAll()
{
    bool hadData = !m_balances.isEmpty() || !m_transactions.isEmpty();
    m_balances.clear();
    m_transactions.clear();

    if (hadData) {
        emit changed();
    }
}

QList<TransactionRow> SpoofMode::transactions(quint32 accountIndex) const
{
    if (!m_enabled) {
        return {};
    }
    return m_transactions.value(accountIndex);
}

bool SpoofMode::simulateSend(Wallet *wallet,
                             const QVector<QString> &addresses,
                             const QVector<quint64> &amounts,
                             const QString &description,
                             QString *error)
{
    if (!m_enabled) {
        if (error) *error = "Spoof mode is not enabled.";
        return false;
    }

    if (!wallet || addresses.isEmpty() || addresses.size() != amounts.size()) {
        if (error) *error = "Invalid simulated transaction request.";
        return false;
    }

    quint64 total = 0;
    for (quint64 amount : amounts) {
        total += amount;
    }

    const quint32 from = wallet->currentSubaddressAccount();
    const quint64 available = balance(from, wallet->unlockedBalance(from));
    const quint64 debit = total + kSpoofFeeAtomic;

    if (debit > available) {
        if (error) *error = "Balance is insufficient for this simulated transfer.";
        return false;
    }

    const QString txid = makeSpoofTxId();
    const QDateTime now = QDateTime::currentDateTime();

    TransactionRow outgoing;
    outgoing.amount = static_cast<qint64>(total);
    outgoing.balanceDelta = -static_cast<qint64>(debit);
    outgoing.fee = kSpoofFeeAtomic;
    outgoing.direction = TransactionRow::Direction_Out;
    outgoing.hash = txid;
    outgoing.description = description.isEmpty() ? QStringLiteral("Transfer") : description;
    outgoing.subaddrAccount = from;
    outgoing.timestamp = now;
    outgoing.pending = true;
    outgoing.confirmations = 0;
    outgoing.unlockTime = kMaxSpoofConfirmations;

    for (int i = 0; i < addresses.size(); ++i) {
        outgoing.transfers.emplace_back(amounts[i], addresses[i]);
    }

    m_balances[from] = available - debit;
    m_transactions[from].prepend(outgoing);

    for (int i = 0; i < addresses.size(); ++i) {
        const SubaddressIndex idx = wallet->subaddressIndex(addresses[i]);
        if (!idx.isValid()) {
            // Not one of our own addresses. If the destination belongs to the
            // linked peer wallet (e.g. Monero GUI), forward it over the bridge
            // so the peer shows an incoming larp transfer.
            if (m_bridge && m_bridge->isRemoteAddress(addresses[i])) {
                m_bridge->sendSpoofedTx(txid, amounts[i], addresses[i],
                                        outgoing.description, now.toMSecsSinceEpoch());
            }
            continue;
        }

        const quint32 toAccount = static_cast<quint32>(idx.major);
        const quint64 current = balance(toAccount, wallet->unlockedBalance(toAccount));
        m_balances[toAccount] = current + amounts[i];

        TransactionRow incoming;
        incoming.amount = static_cast<qint64>(amounts[i]);
        incoming.balanceDelta = static_cast<qint64>(amounts[i]);
        incoming.fee = 0;
        incoming.direction = TransactionRow::Direction_In;
        incoming.hash = txid;
        incoming.description = description.isEmpty() ? QStringLiteral("Transfer") : description;
        incoming.subaddrAccount = toAccount;
        incoming.subaddrIndex.insert(static_cast<quint32>(idx.minor));
        incoming.timestamp = now;
        incoming.pending = true;
        incoming.confirmations = 0;
        incoming.unlockTime = kMaxSpoofConfirmations;
        incoming.transfers.emplace_back(amounts[i], addresses[i]);

        m_transactions[toAccount].prepend(incoming);
    }

    if (!m_confirmationTimer.isActive()) {
        m_confirmationTimer.start();
    }

    emit changed();
    return true;
}

void SpoofMode::advanceConfirmations()
{
    if (!m_enabled) {
        return;
    }

    bool didChange = false;
    for (auto it = m_transactions.begin(); it != m_transactions.end(); ++it) {
        for (TransactionRow &row : it.value()) {
            if (row.confirmations >= kMaxSpoofConfirmations) {
                if (row.pending) {
                    row.pending = false;
                    didChange = true;
                }
                continue;
            }

            row.confirmations++;
            row.pending = false;
            didChange = true;
        }
    }

    if (didChange) {
        emit changed();
    }
}

QString SpoofMode::makeSpoofTxId() const
{
    QString out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        out += QString("%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    }
    return out.left(64);
}
