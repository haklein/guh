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

#include "ruleshandler.h"

#include "guhcore.h"
#include "ruleengine.h"
#include "loggingcategories.h"

#include <QDebug>

RulesHandler::RulesHandler(QObject *parent) :
    JsonHandler(parent)
{
    QVariantMap params;
    QVariantMap returns;

    params.clear(); returns.clear();
    setDescription("GetRules", "Get the descriptions of all configured rules. If you need more information about a specific rule use the "
                   "method Rules.GetRuleDetails.");
    setParams("GetRules", params);
    returns.insert("ruleDescriptions", QVariantList() << JsonTypes::ruleDescriptionRef());
    setReturns("GetRules", returns);

    params.clear(); returns.clear();
    setDescription("GetRuleDetails", "Get details for the rule identified by ruleId");
    params.insert("ruleId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setParams("GetRuleDetails", params);
    returns.insert("o:rule", JsonTypes::ruleRef());
    returns.insert("ruleError", JsonTypes::ruleErrorRef());
    setReturns("GetRuleDetails", returns);

    params.clear(); returns.clear();
    setDescription("AddRule", "Add a rule. You can describe rules by one or many EventDesciptors and a StateEvaluator. Note that only "
                   "one of either eventDescriptor or eventDescriptorList may be passed at a time. A rule can be created but left disabled, "
                   "meaning it won't actually be executed until set to enabled. If not given, enabled defaults to true.");
    params.insert("o:eventDescriptor", JsonTypes::eventDescriptorRef());
    params.insert("o:eventDescriptorList", QVariantList() << JsonTypes::eventDescriptorRef());
    params.insert("o:stateEvaluator", JsonTypes::stateEvaluatorRef());
    params.insert("o:exitActions", QVariantList() << JsonTypes::ruleActionRef());
    params.insert("o:enabled", JsonTypes::basicTypeToString(JsonTypes::Bool));
    params.insert("name", JsonTypes::basicTypeToString(JsonTypes::String));
    QVariantList actions;
    actions.append(JsonTypes::ruleActionRef());
    params.insert("actions", actions);
    setParams("AddRule", params);
    returns.insert("ruleError", JsonTypes::ruleErrorRef());
    returns.insert("o:ruleId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setReturns("AddRule", returns);

    params.clear(); returns.clear();
    setDescription("RemoveRule", "Remove a rule");
    params.insert("ruleId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setParams("RemoveRule", params);
    returns.insert("ruleError", JsonTypes::ruleErrorRef());
    setReturns("RemoveRule", returns);

    params.clear(); returns.clear();
    setDescription("FindRules", "Find a list of rules containing any of the given parameters.");
    params.insert("deviceId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setParams("FindRules", params);
    returns.insert("ruleIds", QVariantList() << JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setReturns("FindRules", returns);

    params.clear(); returns.clear();
    setDescription("EnableRule", "Enabled a rule that has previously been disabled.");
    params.insert("ruleId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setParams("EnableRule", params);
    returns.insert("ruleError", JsonTypes::ruleErrorRef());
    setReturns("EnableRule", returns);

    params.clear(); returns.clear();
    setDescription("DisableRule", "Disable a rule. The rule won't be triggered by it's events or state changes while it is disabled.");
    params.insert("ruleId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setParams("DisableRule", params);
    returns.insert("ruleError", JsonTypes::ruleErrorRef());
    setReturns("DisableRule", returns);

    // Notifications
    params.clear(); returns.clear();
    setDescription("RuleRemoved", "Emitted whenever a Rule was removed.");
    params.insert("ruleId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    setParams("RuleRemoved", params);

    params.clear(); returns.clear();
    setDescription("RuleAdded", "Emitted whenever a Rule was added.");
    params.insert("rule", JsonTypes::ruleRef());
    setParams("RuleAdded", params);

    params.clear(); returns.clear();
    setDescription("RuleActiveChanged", "Emitted whenever the active state of a Rule changed.");
    params.insert("ruleId", JsonTypes::basicTypeToString(JsonTypes::Uuid));
    params.insert("active", JsonTypes::basicTypeToString(JsonTypes::Bool));
    setParams("RuleActiveChanged", params);

    connect(GuhCore::instance(), &GuhCore::ruleAdded, this, &RulesHandler::ruleAddedNotification);
    connect(GuhCore::instance(), &GuhCore::ruleRemoved, this, &RulesHandler::ruleRemovedNotification);
    connect(GuhCore::instance(), &GuhCore::ruleActiveChanged, this, &RulesHandler::ruleActiveChangedNotification);
}

QString RulesHandler::name() const
{
    return "Rules";
}

JsonReply* RulesHandler::GetRules(const QVariantMap &params)
{
    Q_UNUSED(params)

    QVariantList rulesList;
    foreach (const Rule &rule, GuhCore::instance()->rules()) {
        rulesList.append(JsonTypes::packRuleDescription(rule));
    }
    QVariantMap returns;
    returns.insert("ruleDescriptions", rulesList);

    return createReply(returns);
}

JsonReply *RulesHandler::GetRuleDetails(const QVariantMap &params)
{
    RuleId ruleId = RuleId(params.value("ruleId").toString());
    Rule rule = GuhCore::instance()->findRule(ruleId);
    if (rule.id().isNull()) {
        return createReply(statusToReply(RuleEngine::RuleErrorRuleNotFound));
    }
    QVariantMap returns = statusToReply(RuleEngine::RuleErrorNoError);
    returns.insert("rule", JsonTypes::packRule(rule));
    return createReply(returns);
}

JsonReply* RulesHandler::AddRule(const QVariantMap &params)
{
    if (params.contains("eventDescriptor") && params.contains("eventDescriptorList")) {
        QVariantMap returns;
        qCWarning(dcJsonRpc) << "Only one of eventDesciptor or eventDescriptorList may be used.";
        returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorInvalidParameter));
        return createReply(returns);
    }

    if (params.contains("eventDescriptor") || params.contains("eventDescriptorList")) {
        if (params.contains("exitActions")) {
            QVariantMap returns;
            qCWarning(dcJsonRpc) << "The exitActions will never be executed if the rule contains an eventDescriptor.";
            returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorInvalidRuleFormat));
            return createReply(returns);
        }
    }

    // Check and unpack eventDescriptors
    QList<EventDescriptor> eventDescriptorList;
    if (params.contains("eventDescriptor")) {
        QVariantMap eventMap = params.value("eventDescriptor").toMap();
        qCDebug(dcJsonRpc) << "unpacking eventDescriptor" << eventMap;
        eventDescriptorList.append(JsonTypes::unpackEventDescriptor(eventMap));
    } else if (params.contains("eventDescriptorList")) {
        QVariantList eventDescriptors = params.value("eventDescriptorList").toList();
        qCDebug(dcJsonRpc) << "unpacking eventDescriptorList:" << eventDescriptors;
        foreach (const QVariant &eventVariant, eventDescriptors) {
            QVariantMap eventMap = eventVariant.toMap();
            eventDescriptorList.append(JsonTypes::unpackEventDescriptor(eventMap));
        }
    }

    // Check and unpack stateEvaluator
    qCDebug(dcJsonRpc) << "unpacking stateEvaluator:" << params.value("stateEvaluator").toMap();
    StateEvaluator stateEvaluator = JsonTypes::unpackStateEvaluator(params.value("stateEvaluator").toMap());

    // Check and unpack actions
    QList<RuleAction> actions;
    QVariantList actionList = params.value("actions").toList();
    qCDebug(dcJsonRpc) << "unpacking actions:" << actionList;
    foreach (const QVariant &actionVariant, actionList) {
        QVariantMap actionMap = actionVariant.toMap();
        RuleAction action(ActionTypeId(actionMap.value("actionTypeId").toString()), DeviceId(actionMap.value("deviceId").toString()));
        RuleActionParamList actionParamList = JsonTypes::unpackRuleActionParams(actionMap.value("ruleActionParams").toList());
        foreach (const RuleActionParam &ruleActionParam, actionParamList) {
            if (!ruleActionParam.isValid()) {
                qCWarning(dcJsonRpc) << "got an actionParam with value AND eventTypeId!";
                QVariantMap returns;
                returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorInvalidRuleActionParameter));
                return createReply(returns);
            }
        }

        action.setRuleActionParams(actionParamList);
        actions.append(action);
    }

    // check possible eventTypeIds in params
    foreach (const RuleAction &ruleAction, actions) {
        if (ruleAction.isEventBased()) {
            foreach (const RuleActionParam &ruleActionParam, ruleAction.ruleActionParams()) {
                if (ruleActionParam.eventTypeId() != EventTypeId()) {
                    // We have an eventTypeId
                    if (eventDescriptorList.isEmpty()) {
                        QVariantMap returns;
                        qCWarning(dcJsonRpc) << "RuleAction" << ruleAction.actionTypeId() << "contains an eventTypeId, but there are no eventDescriptors.";
                        returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorInvalidRuleActionParameter));
                        return createReply(returns);
                    }
                    // now check if this eventType is in the eventDescriptorList of this rule
                    if (!checkEventDescriptors(eventDescriptorList, ruleActionParam.eventTypeId())) {
                        QVariantMap returns;
                        qCWarning(dcJsonRpc) << "eventTypeId from RuleAction" << ruleAction.actionTypeId() << "missing in eventDescriptors.";
                        returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorInvalidRuleActionParameter));
                        return createReply(returns);
                    }

                    // check if the param type of the event and the action match
                    QVariant::Type eventParamType = getEventParamType(ruleActionParam.eventTypeId(), ruleActionParam.eventParamName());
                    QVariant::Type actionParamType = getActionParamType(ruleAction.actionTypeId(), ruleActionParam.name());
                    if (eventParamType != actionParamType) {
                        QVariantMap returns;
                        qCWarning(dcJsonRpc) << "RuleActionParam" << ruleActionParam.name() << " and given event param " << ruleActionParam.eventParamName() << "have not the same type:";
                        qCWarning(dcJsonRpc) << "        -> actionParamType:" << actionParamType;
                        qCWarning(dcJsonRpc) << "        ->  eventParamType:" << eventParamType;
                        returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorTypesNotMatching));
                        return createReply(returns);
                    }
                }
            }
        }
    }


    QVariantMap returns;
    if (actions.count() == 0) {
        returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorMissingParameter));
        return createReply(returns);
    }

    // Check and unpack exitActions
    QList<RuleAction> exitActions;
    if (params.contains("exitActions")) {
        QVariantList exitActionList = params.value("exitActions").toList();
        qCDebug(dcJsonRpc) << "unpacking exitActions:" << exitActionList;
        foreach (const QVariant &actionVariant, exitActionList) {
            QVariantMap actionMap = actionVariant.toMap();
            RuleAction action(ActionTypeId(actionMap.value("actionTypeId").toString()), DeviceId(actionMap.value("deviceId").toString()));
            if (action.isEventBased()) {
                qCWarning(dcJsonRpc) << "got exitAction with a param value containing an eventTypeId!";
                QVariantMap returns;
                returns.insert("ruleError", JsonTypes::ruleErrorToString(RuleEngine::RuleErrorInvalidRuleActionParameter));
                return createReply(returns);
            }
            action.setRuleActionParams(JsonTypes::unpackRuleActionParams(actionMap.value("ruleActionParams").toList()));
            qCDebug(dcJsonRpc) << "params in exitAction" << action.ruleActionParams();
            exitActions.append(action);
        }
    }


    QString name = params.value("name", QString()).toString();
    bool enabled = params.value("enabled", true).toBool();

    RuleId newRuleId = RuleId::createRuleId();
    RuleEngine::RuleError status = GuhCore::instance()->addRule(newRuleId, name, eventDescriptorList, stateEvaluator, actions, exitActions, enabled);
    if (status ==  RuleEngine::RuleErrorNoError) {
        returns.insert("ruleId", newRuleId.toString());
    }
    returns.insert("ruleError", JsonTypes::ruleErrorToString(status));
    return createReply(returns);
}

JsonReply* RulesHandler::RemoveRule(const QVariantMap &params)
{
    QVariantMap returns;
    RuleId ruleId(params.value("ruleId").toString());
    RuleEngine::RuleError status = GuhCore::instance()->removeRule(ruleId);
    returns.insert("ruleError", JsonTypes::ruleErrorToString(status));
    return createReply(returns);
}

JsonReply *RulesHandler::FindRules(const QVariantMap &params)
{
    DeviceId deviceId = DeviceId(params.value("deviceId").toString());
    QList<RuleId> rules = GuhCore::instance()->findRules(deviceId);

    QVariantList rulesList;
    foreach (const RuleId &ruleId, rules) {
        rulesList.append(ruleId);
    }

    QVariantMap returns;
    returns.insert("ruleIds", rulesList);
    return createReply(returns);
}

JsonReply *RulesHandler::EnableRule(const QVariantMap &params)
{
    return createReply(statusToReply(GuhCore::instance()->enableRule(RuleId(params.value("ruleId").toString()))));
}

JsonReply *RulesHandler::DisableRule(const QVariantMap &params)
{
    return createReply(statusToReply(GuhCore::instance()->disableRule(RuleId(params.value("ruleId").toString()))));
}

QVariant::Type RulesHandler::getActionParamType(const ActionTypeId &actionTypeId, const QString &paramName)
{
    foreach (const DeviceClass &deviceClass, GuhCore::instance()->supportedDevices()) {
        foreach (const ActionType &actionType, deviceClass.actionTypes()) {
            if (actionType.id() == actionTypeId) {
                foreach (const ParamType &paramType, actionType.paramTypes()) {
                    if (paramType.name() == paramName) {
                        return paramType.type();
                    }
                }
            }
        }
    }
    return QVariant::Invalid;
}

QVariant::Type RulesHandler::getEventParamType(const EventTypeId &eventTypeId, const QString &paramName)
{
    foreach (const DeviceClass &deviceClass, GuhCore::instance()->supportedDevices()) {
        foreach (const EventType &eventType, deviceClass.eventTypes()) {
            if (eventType.id() == eventTypeId) {
                foreach (const ParamType &paramType, eventType.paramTypes()) {
                    // get ParamType of Event
                    if (paramType.name() == paramName) {
                        return paramType.type();
                    }
                }
            }
        }
    }
    return QVariant::Invalid;
}

bool RulesHandler::checkEventDescriptors(const QList<EventDescriptor> eventDescriptors, const EventTypeId &eventTypeId)
{
    foreach (const EventDescriptor eventDescriptor, eventDescriptors) {
        if (eventDescriptor.eventTypeId() == eventTypeId) {
            return true;
        }
    }
    return false;
}

void RulesHandler::ruleRemovedNotification(const RuleId &ruleId)
{
    QVariantMap params;
    params.insert("ruleId", ruleId);

    emit RuleRemoved(params);
}

void RulesHandler::ruleAddedNotification(const Rule &rule)
{
    QVariantMap params;
    params.insert("rule", JsonTypes::packRule(rule));

    emit RuleAdded(params);
}

void RulesHandler::ruleActiveChangedNotification(const Rule &rule)
{
    QVariantMap params;
    params.insert("ruleId", rule.id());
    params.insert("active", rule.active());

    emit RuleActiveChanged(params);
}
