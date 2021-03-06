/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                         *
 *  Copyright (C) 2015 Simon Stuerz <simon.stuerz@guh.guru>                *
 *  Copyright (C) 2014 Michael Zanetti <michael_zanetti@gmx.net>           *
 *                                                                         *
 *  This file is part of guh.                                              *
 *                                                                         *
 *  Guh is free software: you can redistribute it and/or modify            *
 *  it under the terms of the GNU General Public License as published by   *
 *  the Free Software Foundation, version 2 of the License.                *
 *                                                                         *
 *  Guh is distributed in the hope that it will be useful,                 *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *  GNU General Public License for more details.                           *
 *                                                                         *
 *  You should have received a copy of the GNU General Public License      *
 *  along with guh. If not, see <http://www.gnu.org/licenses/>.            *
 *                                                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "jsonrpcserver.h"
#include "jsontypes.h"

#ifdef TESTING_ENABLED
#include "mocktcpserver.h"
#else
#include "tcpserver.h"
#endif
#include "jsonhandler.h"

#include "guhcore.h"
#include "devicemanager.h"
#include "plugin/deviceplugin.h"
#include "plugin/deviceclass.h"
#include "plugin/device.h"
#include "rule.h"
#include "ruleengine.h"
#include "loggingcategories.h"

#include "devicehandler.h"
#include "actionhandler.h"
#include "ruleshandler.h"
#include "eventhandler.h"
#include "logginghandler.h"
#include "statehandler.h"

#include <QJsonDocument>
#include <QStringList>

JsonRPCServer::JsonRPCServer(QObject *parent):
    JsonHandler(parent),
#ifdef TESTING_ENABLED
    m_tcpServer(new MockTcpServer(this)),
#else
    m_tcpServer(new TcpServer(this)),
#endif
    m_notificationId(0)
{
    // First, define our own JSONRPC methods
    QVariantMap returns;
    QVariantMap params;

    params.clear(); returns.clear();
    setDescription("Introspect", "Introspect this API.");
    setParams("Introspect", params);
    returns.insert("methods", JsonTypes::basicTypeToString(JsonTypes::Object));
    returns.insert("types", JsonTypes::basicTypeToString(JsonTypes::Object));
    setReturns("Introspect", returns);

    params.clear(); returns.clear();
    setDescription("Version", "Version of this Guh/JSONRPC interface.");
    setParams("Version", params);
    returns.insert("version", JsonTypes::basicTypeToString(JsonTypes::String));
    returns.insert("protocol version", JsonTypes::basicTypeToString(JsonTypes::String));
    setReturns("Version", returns);

    params.clear(); returns.clear();
    setDescription("SetNotificationStatus", "Enable/Disable notifications for this connections.");
    params.insert("enabled", JsonTypes::basicTypeToString(JsonTypes::Bool));
    setParams("SetNotificationStatus", params);
    returns.insert("enabled", JsonTypes::basicTypeToString(JsonTypes::Bool));
    setReturns("SetNotificationStatus", returns);

    // Now set up the logic
    connect(m_tcpServer, SIGNAL(clientConnected(const QUuid &)), this, SLOT(clientConnected(const QUuid &)));
    connect(m_tcpServer, SIGNAL(clientDisconnected(const QUuid &)), this, SLOT(clientDisconnected(const QUuid &)));
    connect(m_tcpServer, SIGNAL(dataAvailable(const QUuid &, QByteArray)), this, SLOT(processData(const QUuid &, QByteArray)));
    m_tcpServer->startServer();

    QMetaObject::invokeMethod(this, "setup", Qt::QueuedConnection);
}

QString JsonRPCServer::name() const
{
    return QStringLiteral("JSONRPC");
}

JsonReply* JsonRPCServer::Introspect(const QVariantMap &params) const
{
    Q_UNUSED(params)

    QVariantMap data;
    data.insert("types", JsonTypes::allTypes());
    QVariantMap methods;
    foreach (JsonHandler *handler, m_handlers) {
        methods.unite(handler->introspect(QMetaMethod::Method));
    }
    data.insert("methods", methods);

    QVariantMap signalsMap;
    foreach (JsonHandler *handler, m_handlers) {
        signalsMap.unite(handler->introspect(QMetaMethod::Signal));
    }
    data.insert("notifications", signalsMap);

    return createReply(data);
}

JsonReply* JsonRPCServer::Version(const QVariantMap &params) const
{
    Q_UNUSED(params)

    QVariantMap data;
    data.insert("version", GUH_VERSION_STRING);
    data.insert("protocol version", JSON_PROTOCOL_VERSION);
    return createReply(data);
}

JsonReply* JsonRPCServer::SetNotificationStatus(const QVariantMap &params)
{
    QUuid clientId = this->property("clientId").toUuid();
    m_clients[clientId] = params.value("enabled").toBool();
    QVariantMap returns;
    returns.insert("enabled", m_clients[clientId]);
    return createReply(returns);
}

void JsonRPCServer::setup()
{
    registerHandler(this);
    registerHandler(new DeviceHandler(this));
    registerHandler(new ActionHandler(this));
    registerHandler(new RulesHandler(this));
    registerHandler(new EventHandler(this));
    registerHandler(new LoggingHandler(this));
    registerHandler(new StateHandler(this));
}

void JsonRPCServer::processData(const QUuid &clientId, const QByteArray &jsonData)
{
    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &error);

    if(error.error != QJsonParseError::NoError) {
        qCWarning(dcJsonRpc) << "failed to parse data" << jsonData << ":" << error.errorString();
    }

    QVariantMap message = jsonDoc.toVariant().toMap();

    bool success;
    int commandId = message.value("id").toInt(&success);
    if (!success) {
        qCWarning(dcJsonRpc) << "Error parsing command. Missing \"id\":" << jsonData;
        sendErrorResponse(clientId, commandId, "Error parsing command. Missing 'id'");
        return;
    }

    QStringList commandList = message.value("method").toString().split('.');
    if (commandList.count() != 2) {
        qCWarning(dcJsonRpc) << "Error parsing method.\nGot:" << message.value("method").toString() << "\nExpected: \"Namespace.method\"";
        sendErrorResponse(clientId, commandId, QString("Error parsing method. Got: '%1'', Expected: 'Namespace.method'").arg(message.value("method").toString()));
        return;
    }

    QString targetNamespace = commandList.first();
    QString method = commandList.last();
    QVariantMap params = message.value("params").toMap();

    emit commandReceived(targetNamespace, method, params);

    JsonHandler *handler = m_handlers.value(targetNamespace);
    if (!handler) {
        sendErrorResponse(clientId, commandId, "No such namespace");
        return;
    }
    if (!handler->hasMethod(method)) {
        sendErrorResponse(clientId, commandId, "No such method");
        return;
    }
    QPair<bool, QString> validationResult = handler->validateParams(method, params);
    if (!validationResult.first) {
        sendErrorResponse(clientId, commandId, "Invalid params: " + validationResult.second);
        return;
    }

    // Hack: attach clientId to handler to be able to handle the JSONRPC methods. Do not use this outside of jsonrpcserver
    handler->setProperty("clientId", clientId);

    JsonReply *reply;
    QMetaObject::invokeMethod(handler, method.toLatin1().data(), Q_RETURN_ARG(JsonReply*, reply), Q_ARG(QVariantMap, params));
    if (reply->type() == JsonReply::TypeAsync) {
        reply->setClientId(clientId);
        reply->setCommandId(commandId);
        connect(reply, &JsonReply::finished, this, &JsonRPCServer::asyncReplyFinished);
        reply->startWait();
    } else {
        Q_ASSERT_X((targetNamespace == "JSONRPC" && method == "Introspect") || handler->validateReturns(method, reply->data()).first
                   ,"validating return value", formatAssertion(targetNamespace, method, handler, reply->data()).toLatin1().data());
        sendResponse(clientId, commandId, reply->data());
        reply->deleteLater();
    }
}

QString JsonRPCServer::formatAssertion(const QString &targetNamespace, const QString &method, JsonHandler *handler, const QVariantMap &data) const
{
    QJsonDocument doc = QJsonDocument::fromVariant(handler->introspect(QMetaMethod::Method).value(targetNamespace + "." + method));
    QJsonDocument doc2 = QJsonDocument::fromVariant(data);
    return QString("\nMethod: %1\nTemplate: %2\nValue: %3")
            .arg(targetNamespace + "." + method)
            .arg(QString(doc.toJson()))
            .arg(QString(doc2.toJson()));
}

void JsonRPCServer::sendNotification(const QVariantMap &params)
{
    JsonHandler *handler = qobject_cast<JsonHandler*>(sender());
    QMetaMethod method = handler->metaObject()->method(senderSignalIndex());

    QVariantMap notification;
    notification.insert("id", m_notificationId++);
    notification.insert("notification", handler->name() + "." + method.name());
    notification.insert("params", params);

    QJsonDocument jsonDoc = QJsonDocument::fromVariant(notification);
    m_tcpServer->sendData(m_clients.keys(true), jsonDoc.toJson());
}

void JsonRPCServer::asyncReplyFinished()
{
    JsonReply *reply = qobject_cast<JsonReply*>(sender());
    if (!reply->timedOut()) {
        Q_ASSERT_X(reply->handler()->validateReturns(reply->method(), reply->data()).first
                   ,"validating return value", formatAssertion(reply->handler()->name(), reply->method(), reply->handler(), reply->data()).toLatin1().data());
        sendResponse(reply->clientId(), reply->commandId(), reply->data());
    } else {
        sendErrorResponse(reply->clientId(), reply->commandId(), "Command timed out");
    }
    reply->deleteLater();
}

void JsonRPCServer::registerHandler(JsonHandler *handler)
{
    m_handlers.insert(handler->name(), handler);
    for (int i = 0; i < handler->metaObject()->methodCount(); ++i) {
        QMetaMethod method = handler->metaObject()->method(i);
        if (method.methodType() == QMetaMethod::Signal && QString(method.name()).contains(QRegExp("^[A-Z]"))) {
            QObject::connect(handler, method, this, metaObject()->method(metaObject()->indexOfSlot("sendNotification(QVariantMap)")));
        }
    }
}

void JsonRPCServer::clientConnected(const QUuid &clientId)
{
    // Notifications disabled by default
    m_clients.insert(clientId, false);

    QVariantMap handshake;
    handshake.insert("id", 0);
    handshake.insert("server", "guh JSONRPC interface");
    handshake.insert("version", GUH_VERSION_STRING);
    handshake.insert("protocol version", JSON_PROTOCOL_VERSION);
    QJsonDocument jsonDoc = QJsonDocument::fromVariant(handshake);
    m_tcpServer->sendData(clientId, jsonDoc.toJson());
}

void JsonRPCServer::clientDisconnected(const QUuid &clientId)
{
    m_clients.remove(clientId);
}

void JsonRPCServer::sendResponse(const QUuid &clientId, int commandId, const QVariantMap &params)
{
    QVariantMap rsp;
    rsp.insert("id", commandId);
    rsp.insert("status", "success");
    rsp.insert("params", params);

    QJsonDocument jsonDoc = QJsonDocument::fromVariant(rsp);
    m_tcpServer->sendData(clientId, jsonDoc.toJson());
}

void JsonRPCServer::sendErrorResponse(const QUuid &clientId, int commandId, const QString &error)
{
    QVariantMap rsp;
    rsp.insert("id", commandId);
    rsp.insert("status", "error");
    rsp.insert("error", error);

    QJsonDocument jsonDoc = QJsonDocument::fromVariant(rsp);
    m_tcpServer->sendData(clientId, jsonDoc.toJson());
}


