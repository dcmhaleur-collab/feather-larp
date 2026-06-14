// SPDX-License-Identifier: BSD-3-Clause

#ifndef FEATHER_SPOOFMODEDIALOG_H
#define FEATHER_SPOOFMODEDIALOG_H

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QTableWidget;
class Wallet;

class SpoofModeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpoofModeDialog(Wallet *wallet, QWidget *parent = nullptr);

private slots:
    void apply();

private:
    void refreshWalletViews();

    Wallet *m_wallet;
    QCheckBox *m_enabled;
    QTableWidget *m_table;
};

#endif // FEATHER_SPOOFMODEDIALOG_H
