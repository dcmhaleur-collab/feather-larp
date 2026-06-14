// SPDX-License-Identifier: BSD-3-Clause

#include "SpoofModeDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "SpoofMode.h"
#include "libwalletqt/SubaddressAccount.h"
#include "libwalletqt/TransactionHistory.h"
#include "libwalletqt/Wallet.h"
#include "libwalletqt/WalletManager.h"

SpoofModeDialog::SpoofModeDialog(Wallet *wallet, QWidget *parent)
    : QDialog(parent)
    , m_wallet(wallet)
    , m_enabled(new QCheckBox("Enable spoofed balances", this))
    , m_table(new QTableWidget(this))
{
    setWindowTitle("Spoof Mode");
    resize(760, 360);

    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(
        "Spoof Mode is local-only and visibly simulated. It changes displayed balances and adds simulated history rows; "
        "it does not create, sign, broadcast, or prove real blockchain transactions.", this);
    notice->setWordWrap(true);

    layout->addWidget(notice);
    layout->addWidget(m_enabled);
    layout->addWidget(m_table);

    m_enabled->setChecked(SpoofMode::instance()->isEnabled());

    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Account", "Label", "Primary address", "Spoofed balance (XMR)"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);

    const int accounts = static_cast<int>(m_wallet->numSubaddressAccounts());
    m_table->setRowCount(accounts);

    for (int row = 0; row < accounts; ++row) {
        const quint32 account = static_cast<quint32>(row);

        auto *accountItem = new QTableWidgetItem(QString("#%1").arg(row));
        accountItem->setData(Qt::UserRole, account);
        accountItem->setFlags(accountItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, accountItem);

        auto *labelItem = new QTableWidgetItem(m_wallet->getSubaddressLabel(account, 0));
        labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 1, labelItem);

        auto *addressItem = new QTableWidgetItem(m_wallet->address(account, 0));
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 2, addressItem);

        auto *spin = new QDoubleSpinBox(this);
        spin->setDecimals(12);
        spin->setMinimum(0.0);
        spin->setMaximum(1000000000.0);
        spin->setSingleStep(1.0);
        spin->setValue(SpoofMode::instance()->balance(account, m_wallet->unlockedBalance(account)) / 1000000000000.0);
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            m_enabled->setChecked(true);
        });
        m_table->setCellWidget(row, 3, spin);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SpoofModeDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &SpoofModeDialog::reject);
    layout->addWidget(buttons);
}

void SpoofModeDialog::apply()
{
    auto *mode = SpoofMode::instance();
    const bool enable = m_enabled->isChecked();

    if (enable) {
        QHash<quint32, quint64> balances;
        for (int row = 0; row < m_table->rowCount(); ++row) {
            auto *spin = qobject_cast<QDoubleSpinBox *>(m_table->cellWidget(row, 3));
            auto *accountItem = m_table->item(row, 0);
            if (!spin || !accountItem) {
                continue;
            }

            const quint32 account = accountItem->data(Qt::UserRole).toUInt();
            balances.insert(
                account,
                WalletManager::amountFromString(QString::number(spin->value(), 'f', 12)));
        }

        mode->replaceBalances(balances);
    }

    mode->setEnabled(enable);
    refreshWalletViews();
}

void SpoofModeDialog::refreshWalletViews()
{
    if (!m_wallet) {
        return;
    }

    m_wallet->subaddressAccount()->refresh();
    m_wallet->history()->refresh();
    m_wallet->updateBalance();
}
