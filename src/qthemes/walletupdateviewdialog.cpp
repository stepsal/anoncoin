// Copyright (c) 2013-2015 The Anoncoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletupdateviewdialog.h"
#include "ui_walletupdateviewdialog.h"

WalletUpdateViewDialog::WalletUpdateViewDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::WalletUpdateViewDialog)
{
    ui->setupUi(this);
}

WalletUpdateViewDialog::~WalletUpdateViewDialog()
{
    delete ui;
}
