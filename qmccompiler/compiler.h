/*!
 * Copyright (C) 2014 Nomovok Ltd. All rights reserved.
 * Contact: info@nomovok.com
 *
 * This file may be used under the terms of the GNU Lesser
 * General Public License version 2.1 as published by the Free Software
 * Foundation and appearing in the file LICENSE.LGPL included in the
 * packaging of this file.  Please review the following information to
 * ensure the GNU Lesser General Public License version 2.1 requirements
 * will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 *
 * In addition, as a special exception, copyright holders
 * give you certain additional rights.  These rights are described in
 * the Digia Qt LGPL Exception version 1.1, included in the file
 * LGPL_EXCEPTION.txt in this package.
 *
 * Author: Mikko Hurskainen <mikko.hurskainen@nomovok.com>
 */

#ifndef COMPILER_H
#define COMPILER_H

#include <QObject>
#include <QQmlError>
#include <QList>
#include <QDataStream>
#include <QUrl>

class QmlCompilation;
class CompilerPrivate;

#include "qmccompiler_global.h"

class QMCCOMPILERSHARED_EXPORT Compiler : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(Compiler)

public:

    Compiler(QObject *parent = 0);
    virtual ~Compiler();
    bool compile(const QString &url, QDataStream &output);

    QList<QQmlError> errors() const;
    bool isError() const;
    const QList<QQmlError>& compileErrors() const;
protected:
    virtual bool compileData(QmlCompilation *compilation) = 0;
    void appendError(const QQmlError &error);
    void appendErrors(const QList<QQmlError> &errors);

private:
    bool exportData(QmlCompilation *compilation, QDataStream &output);
    bool loadData(const QUrl& url, QmlCompilation *compilation);
    void clearError();

    Q_DISABLE_COPY(Compiler)
};

#endif // COMPILER_H