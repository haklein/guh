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

/*!
    \class RuleEngine
    \brief The Engine that evaluates \l{Rule}{Rules} and finds
    \l{Action}{Actions} to be executed.

    \ingroup core
    \inmodule server

    You can add, remove and update rules and query the engine for actions to be executed
    for a given \l{Event} described by an \l{EventDescriptor}.

    \sa Event, EventDescriptor, Rule, RuleAction
*/

/*! \fn void RuleEngine::ruleAdded(const Rule &rule)
    Will be emitted whenever a new \l{Rule} is added to this Engine.
    The \a rule parameter holds the entire new rule.*/

/*! \fn void RuleEngine::ruleRemoved(const RuleId &ruleId)
    Will be emitted whenever a \l{Rule} is removed from this Engine.
    \a ruleId holds the id of the removed rule. You should remove any references
    or copies you hold for this rule.*/

/*! \fn void RuleEngine::ruleChanged(const RuleId &ruleId)
    Will be emitted whenever a \l{Rule} changed his enable/disable status.
    \a ruleId holds the id of the changed rule.*/

/*! \enum RuleEngine::RuleError
    \value RuleErrorNoError
        No error happened. Everything is fine.
    \value RuleErrorInvalidRuleId
        The given RuleId is not valid.
    \value RuleErrorRuleNotFound
        Couldn't find a \l{Rule} with the given id.
    \value RuleErrorDeviceNotFound
        Couldn't find a \l{Device} with the given id.
    \value RuleErrorEventTypeNotFound
        Couldn't find a \l{EventType} with the given id.
    \value RuleErrorActionTypeNotFound
        Couldn't find a \l{ActionType} with the given id.
    \value RuleErrorInvalidParameter
        The given \l{Param} is not valid.
    \value RuleErrorInvalidRuleFormat
        The format of the rule is not valid. (i.e. add \l{Rule} with exitActions and eventDescriptors)
    \value RuleErrorMissingParameter
        One of the given \l{Param}{Params} is missing.
    \value RuleErrorInvalidRuleActionParameter
        One of the given \l{RuleActionParam}{RuleActionParams} is not valid.
    \value RuleErrorTypesNotMatching
        One of the RuleActionParams type does not match with the corresponding EventParam type.
*/

/*! \enum RuleEngine::RemovePolicy
    \value RemovePolicyCascade
        Remove the whole \l{Rule}.
    \value RemovePolicyUpdate
        Remove a \l{Device} from a rule.
*/


#include "ruleengine.h"
#include "loggingcategories.h"
#include "types/paramdescriptor.h"
#include "types/eventdescriptor.h"

#include "guhcore.h"

#include "devicemanager.h"
#include "plugin/device.h"

#include <QSettings>
#include <QDebug>
#include <QStringList>
#include <QStandardPaths>
#include <QCoreApplication>

/*! Constructs the RuleEngine with the given \a parent. Although it wouldn't harm to have multiple RuleEngines, there is one
    instance available from \l{GuhCore}. This one should be used instead of creating multiple ones.
    */
RuleEngine::RuleEngine(QObject *parent) :
    QObject(parent)
{
    m_settingsFile = QCoreApplication::instance()->organizationName() + "/rules";
    QSettings settings(m_settingsFile);
    qCDebug(dcRuleEngine) << "laoding rules from" << settings.fileName();
    foreach (const QString &idString, settings.childGroups()) {
        settings.beginGroup(idString);

        QString name = settings.value("name", idString).toString();
        bool enabled = settings.value("enabled", true).toBool();

        qCDebug(dcRuleEngine) << "load rule" << name << idString;

        QList<EventDescriptor> eventDescriptorList;
        settings.beginGroup("events");

        foreach (QString eventGroupName, settings.childGroups()) {
            if (eventGroupName.startsWith("EventDescriptor-")) {
                settings.beginGroup(eventGroupName);
                EventTypeId eventTypeId(settings.value("eventTypeId").toString());
                DeviceId deviceId(settings.value("deviceId").toString());

                QList<ParamDescriptor> params;
                foreach (QString groupName, settings.childGroups()) {
                    if (groupName.startsWith("ParamDescriptor-")) {
                        settings.beginGroup(groupName);
                        ParamDescriptor paramDescriptor(groupName.remove(QRegExp("^ParamDescriptor-")), settings.value("value"));
                        paramDescriptor.setOperatorType((Types::ValueOperator)settings.value("operator").toInt());
                        params.append(paramDescriptor);
                        settings.endGroup();
                    }
                }

                EventDescriptor eventDescriptor(eventTypeId, deviceId, params);
                eventDescriptorList.append(eventDescriptor);
                settings.endGroup();
            }
        }
        settings.endGroup();

        StateEvaluator stateEvaluator = StateEvaluator::loadFromSettings(settings, "stateEvaluator");
        
        QList<RuleAction> actions;
        settings.beginGroup("ruleActions");
        foreach (const QString &actionNumber, settings.childGroups()) {
            settings.beginGroup(actionNumber);

            RuleAction action = RuleAction(ActionTypeId(settings.value("actionTypeId").toString()),
                                           DeviceId(settings.value("deviceId").toString()));

            RuleActionParamList params;
            foreach (QString paramNameString, settings.childGroups()) {
                if (paramNameString.startsWith("RuleActionParam-")) {
                    settings.beginGroup(paramNameString);
                    RuleActionParam param(paramNameString.remove(QRegExp("^RuleActionParam-")),
                                          settings.value("value",QVariant()),
                                          EventTypeId(settings.value("eventTypeId", EventTypeId()).toString()),
                                          settings.value("eventParamName", QString()).toString());
                    params.append(param);
                    settings.endGroup();
                }
            }

            action.setRuleActionParams(params);
            actions.append(action);

            settings.endGroup();
        }
        settings.endGroup();

        QList<RuleAction> exitActions;
        settings.beginGroup("ruleExitActions");
        foreach (const QString &actionNumber, settings.childGroups()) {
            settings.beginGroup(actionNumber);

            RuleAction action = RuleAction(ActionTypeId(settings.value("actionTypeId").toString()),
                                           DeviceId(settings.value("deviceId").toString()));

            RuleActionParamList params;
            foreach (QString paramNameString, settings.childGroups()) {
                if (paramNameString.startsWith("RuleActionParam-")) {
                    settings.beginGroup(paramNameString);
                    RuleActionParam param(paramNameString.remove(QRegExp("^RuleActionParam-")), settings.value("value"));
                    params.append(param);
                    settings.endGroup();
                }
            }
            action.setRuleActionParams(params);
            exitActions.append(action);
            settings.endGroup();
        }
        settings.endGroup();

        Rule rule = Rule(RuleId(idString), name, eventDescriptorList, stateEvaluator, actions, exitActions);
        rule.setEnabled(enabled);
        appendRule(rule);
        settings.endGroup();
    }
}

/*! Ask the Engine to evaluate all the rules for the given \a event.
    This will search all the \l{Rule}{Rules} triggered by the given \a event
    and evaluate their states in the system. It will return a
    list of all \l{Rule}{Rules} that are triggered or change its active state
    because of this \a event. */
QList<Rule> RuleEngine::evaluateEvent(const Event &event)
{
    Device *device = GuhCore::instance()->findConfiguredDevice(event.deviceId());

    qCDebug(dcRuleEngine) << "got event:" << event << device->name() << event.eventTypeId();

    QList<Rule> rules;
    foreach (const RuleId &id, m_ruleIds) {
        Rule rule = m_rules.value(id);
        if (!rule.enabled()) {
            continue;
        }

        if (rule.eventDescriptors().isEmpty()) {
            // This rule seems to have only states, check on state changed
            if (containsState(rule.stateEvaluator(), event)) {
                if (rule.stateEvaluator().evaluate()) {
                    if (m_activeRules.contains(rule.id())) {
                        qCDebug(dcRuleEngine) << "Rule" << rule.id() << "still in active state.";
                    } else {
                        qCDebug(dcRuleEngine) << "Rule" << rule.id() << "entered active state.";
                        rule.setActive(true);
                        m_rules[rule.id()] = rule;
                        m_activeRules.append(rule.id());
                        rules.append(rule);
                    }
                } else {
                    if (m_activeRules.contains(rule.id())) {
                        qCDebug(dcRuleEngine) << "Rule" << rule.id() << "left active state.";
                        rule.setActive(false);
                        m_rules[rule.id()] = rule;
                        m_activeRules.removeAll(rule.id());
                        rules.append(rule);
                    }
                }
            }
        } else {
            if (containsEvent(rule, event)) {
                if (rule.stateEvaluator().evaluate()) {
                    qCDebug(dcRuleEngine) << "Rule" << rule.id() << "contains event" << event.eventId() << "and all states match.";
                    rules.append(rule);
                }
            }
        }
    }
    return rules;
}

/*! Add a new \l{Rule} with the given \a ruleId , \a name, \a eventDescriptorList, \a actions and \a enabled value to the engine.
    For convenience, this creates a Rule without any \l{State} comparison. */
RuleEngine::RuleError RuleEngine::addRule(const RuleId &ruleId, const QString &name, const QList<EventDescriptor> &eventDescriptorList, const QList<RuleAction> &actions, bool enabled)
{
    return addRule(ruleId, name, eventDescriptorList, StateEvaluator(), actions, QList<RuleAction>(), enabled);
}

/*! Add a new \l{Rule} with the given \a ruleId, \a name, \a eventDescriptorList, \a stateEvaluator, the  list of \a actions the list of \a exitActions and the \a enabled value to the engine.*/
RuleEngine::RuleError RuleEngine::addRule(const RuleId &ruleId, const QString &name, const QList<EventDescriptor> &eventDescriptorList, const StateEvaluator &stateEvaluator, const QList<RuleAction> &actions, const QList<RuleAction> &exitActions, bool enabled)
{
    if (ruleId.isNull()) {
        return RuleErrorInvalidRuleId;
    }
    if (!findRule(ruleId).id().isNull()) {
        qCWarning(dcRuleEngine) << "Already have a rule with this id!";
        return RuleErrorInvalidRuleId;
    }

    // TODO: check rule name for duplicated rulesnames

    foreach (const EventDescriptor &eventDescriptor, eventDescriptorList) {
        Device *device = GuhCore::instance()->findConfiguredDevice(eventDescriptor.deviceId());
        if (!device) {
            qCWarning(dcRuleEngine) << "Cannot create rule. No configured device for eventTypeId" << eventDescriptor.eventTypeId();
            return RuleErrorDeviceNotFound;
        }
        DeviceClass deviceClass = GuhCore::instance()->findDeviceClass(device->deviceClassId());

        bool eventTypeFound = false;
        foreach (const EventType &eventType, deviceClass.eventTypes()) {
            if (eventType.id() == eventDescriptor.eventTypeId()) {
                eventTypeFound = true;
            }
        }
        if (!eventTypeFound) {
            qCWarning(dcRuleEngine) << "Cannot create rule. Device " + device->name() + " has no event type:" << eventDescriptor.eventTypeId();
            return RuleErrorEventTypeNotFound;
        }
    }

    foreach (const RuleAction &action, actions) {
        Device *device = GuhCore::instance()->findConfiguredDevice(action.deviceId());
        if (!device) {
            qCWarning(dcRuleEngine) << "Cannot create rule. No configured device for actionTypeId" << action.actionTypeId();
            return RuleErrorDeviceNotFound;
        }
        DeviceClass deviceClass = GuhCore::instance()->findDeviceClass(device->deviceClassId());

        bool actionTypeFound = false;
        foreach (const ActionType &actionType, deviceClass.actionTypes()) {
            if (actionType.id() == action.actionTypeId()) {
                actionTypeFound = true;
            }
        }
        if (!actionTypeFound) {
            qCWarning(dcRuleEngine) << "Cannot create rule. Device " + device->name() + " has no action type:" << action.actionTypeId();
            return RuleErrorActionTypeNotFound;
        }
    }
    if (actions.count() > 0) {
        qCDebug(dcRuleEngine) << "actions" << actions.last().actionTypeId() << actions.last().ruleActionParams();
    }

    foreach (const RuleAction &action, exitActions) {
        Device *device = GuhCore::instance()->findConfiguredDevice(action.deviceId());
        if (!device) {
            qCWarning(dcRuleEngine) << "Cannot create rule. No configured device for actionTypeId" << action.actionTypeId();
            return RuleErrorDeviceNotFound;
        }
        DeviceClass deviceClass = GuhCore::instance()->findDeviceClass(device->deviceClassId());

        bool actionTypeFound = false;
        foreach (const ActionType &actionType, deviceClass.actionTypes()) {
            if (actionType.id() == action.actionTypeId()) {
                actionTypeFound = true;
            }
        }
        if (!actionTypeFound) {
            qCWarning(dcRuleEngine) << "Cannot create rule. Device " + device->name() + " has no action type:" << action.actionTypeId();
            return RuleErrorActionTypeNotFound;
        }
    }
    if (exitActions.count() > 0) {
        qCDebug(dcRuleEngine) << "exit actions" << exitActions.last().actionTypeId() << exitActions.last().ruleActionParams();
    }

    Rule rule = Rule(ruleId, name, eventDescriptorList, stateEvaluator, actions, exitActions);
    rule.setEnabled(enabled);
    appendRule(rule);
    emit ruleAdded(rule);

    // Save Events / EventDescriptors
    QSettings settings(m_settingsFile);
    settings.beginGroup(rule.id().toString());
    settings.setValue("name", name);
    settings.setValue("enabled", enabled);
    settings.beginGroup("events");
    for (int i = 0; i < eventDescriptorList.count(); i++) {
        const EventDescriptor &eventDescriptor = eventDescriptorList.at(i);
        settings.beginGroup("EventDescriptor-" + QString::number(i));
        settings.setValue("deviceId", eventDescriptor.deviceId().toString());
        settings.setValue("eventTypeId", eventDescriptor.eventTypeId().toString());

        foreach (const ParamDescriptor &paramDescriptor, eventDescriptor.paramDescriptors()) {
            settings.beginGroup("ParamDescriptor-" + paramDescriptor.name());
            settings.setValue("value", paramDescriptor.value());
            settings.setValue("operator", paramDescriptor.operatorType());
            settings.endGroup();
        }
        settings.endGroup();
    }
    settings.endGroup();

    // Save StateEvaluator
    stateEvaluator.dumpToSettings(settings, "stateEvaluator");

    // Save ruleActions
    int i = 0;
    settings.beginGroup("ruleActions");
    foreach (const RuleAction &action, rule.actions()) {
        settings.beginGroup(QString::number(i));
        settings.setValue("deviceId", action.deviceId().toString());
        settings.setValue("actionTypeId", action.actionTypeId().toString());
        foreach (const RuleActionParam &param, action.ruleActionParams()) {
            settings.beginGroup("RuleActionParam-" + param.name());
            settings.setValue("value", param.value());
            if (param.eventTypeId() != EventTypeId()) {
                settings.setValue("eventTypeId", param.eventTypeId().toString());
                settings.setValue("eventParamName", param.eventParamName());
            }
            settings.endGroup();
        }
        i++;
        settings.endGroup();
    }
    settings.endGroup();

    // Save ruleExitActions
    settings.beginGroup("ruleExitActions");
    i = 0;
    foreach (const RuleAction &action, rule.exitActions()) {
        settings.beginGroup(QString::number(i));
        settings.setValue("deviceId", action.deviceId().toString());
        settings.setValue("actionTypeId", action.actionTypeId().toString());
        foreach (const RuleActionParam &param, action.ruleActionParams()) {
            settings.beginGroup("RuleActionParam-" + param.name());
            settings.setValue("value", param.value());
            settings.endGroup();
        }
        i++;
        settings.endGroup();
    }
    settings.endGroup();

    return RuleErrorNoError;
}

/*! Returns a list of all \l{Rule}{Rules} loaded in this Engine.
    Be aware that this does not necessarily reflect the order of the rules in the engine.
    Use ruleIds() if you need the correct order.
*/
QList<Rule> RuleEngine::rules() const
{
    return m_rules.values();
}

/*! Returns a list of all ruleIds loaded in this Engine. */
QList<RuleId> RuleEngine::ruleIds() const
{
    return m_ruleIds;
}

/*! Removes the \l{Rule} with the given \a ruleId from the Engine.
    Returns \l{RuleError} which describes whether the operation
    was successful or not. */
RuleEngine::RuleError RuleEngine::removeRule(const RuleId &ruleId)
{
    int index = m_ruleIds.indexOf(ruleId);

    if (index < 0) {
        return RuleErrorRuleNotFound;
    }

    m_ruleIds.takeAt(index);
    m_rules.remove(ruleId);

    QSettings settings(m_settingsFile);
    settings.beginGroup(ruleId.toString());
    settings.remove("");
    settings.endGroup();

    emit ruleRemoved(ruleId);
    return RuleErrorNoError;
}

/*! Enables the rule with the given \a ruleId that has been previously disabled.
    \sa disableRule(), */
RuleEngine::RuleError RuleEngine::enableRule(const RuleId &ruleId)
{
    if (!m_rules.contains(ruleId)) {
        qCWarning(dcRuleEngine) << "Rule not found. Can't enable it";
        return RuleErrorRuleNotFound;
    }
    Rule rule = m_rules.value(ruleId);
    rule.setEnabled(true);
    m_rules[ruleId] = rule;
    QSettings settings(m_settingsFile);
    settings.beginGroup(ruleId.toString());
    if (!settings.value("enabled", true).toBool()) {
        settings.setValue("enabled", true);
        emit ruleChanged(ruleId);
    }
    settings.endGroup();

    return RuleErrorNoError;
}

/*! Disables the rule with the given \a ruleId. Disabled rules won't be triggered.
    \sa enableRule(), */
RuleEngine::RuleError RuleEngine::disableRule(const RuleId &ruleId)
{
    if (!m_rules.contains(ruleId)) {
        qCWarning(dcRuleEngine) << "Rule not found. Can't disable it";
        return RuleErrorRuleNotFound;
    }
    Rule rule = m_rules.value(ruleId);
    rule.setEnabled(false);
    m_rules[ruleId] = rule;
    QSettings settings(m_settingsFile);
    settings.beginGroup(ruleId.toString());
    if (settings.value("enabled", true).toBool()) {
        settings.setValue("enabled", false);
        emit ruleChanged(ruleId);
    }
    settings.endGroup();

    return RuleErrorNoError;
}

/*! Returns the \l{Rule} with the given \a ruleId. If the \l{Rule} does not exist, it will return \l{Rule::Rule()} */
Rule RuleEngine::findRule(const RuleId &ruleId)
{
    foreach (const Rule &rule, m_rules) {
        if (rule.id() == ruleId) {
            return rule;
        }
    }
    return Rule();
}

/*! Returns a list of all \l{Rule}{Rules} loaded in this Engine, which contains a \l{Device} with the given \a deviceId. */
QList<RuleId> RuleEngine::findRules(const DeviceId &deviceId)
{
    // Find all offending rules
    QList<RuleId> offendingRules;
    foreach (const Rule &rule, m_rules) {
        bool offending = false;
        foreach (const EventDescriptor &eventDescriptor, rule.eventDescriptors()) {
            if (eventDescriptor.deviceId() == deviceId) {
                offending = true;
                break;
            }
        }
        if (!offending && rule.stateEvaluator().containsDevice(deviceId)) {
            offending = true;
        }
        if (!offending) {
            foreach (const RuleAction &action, rule.actions()) {
                if (action.deviceId() == deviceId) {
                    offending = true;
                    break;
                }
            }
        }
        if (offending) {
            offendingRules.append(rule.id());
        }
    }
    return offendingRules;
}

/*! Removes a \l{Device} from a \l{Rule} with the given \a id and \a deviceId. */
void RuleEngine::removeDeviceFromRule(const RuleId &id, const DeviceId &deviceId)
{
    if (!m_rules.contains(id)) {
        return;
    }
    Rule rule = m_rules.value(id);
    QList<EventDescriptor> eventDescriptors = rule.eventDescriptors();
    QList<int> removeIndexes;
    for (int i = 0; i < eventDescriptors.count(); i++) {
        if (eventDescriptors.at(i).deviceId() == deviceId) {
            removeIndexes.append(i);
        }
    }
    while (removeIndexes.count() > 0) {
        eventDescriptors.takeAt(removeIndexes.takeLast());
    }
    StateEvaluator stateEvalatuator = rule.stateEvaluator();
    stateEvalatuator.removeDevice(deviceId);

    QList<RuleAction> actions = rule.actions();
    for (int i = 0; i < actions.count(); i++) {
        if (actions.at(i).deviceId() == deviceId) {
            removeIndexes.append(i);
        }
    }
    while (removeIndexes.count() > 0) {
        actions.takeAt(removeIndexes.takeLast());
    }
    Rule newRule(id, rule.name(), eventDescriptors, stateEvalatuator, actions);
    m_rules[id] = newRule;
}

bool RuleEngine::containsEvent(const Rule &rule, const Event &event)
{
    foreach (const EventDescriptor &eventDescriptor, rule.eventDescriptors()) {
        if (eventDescriptor == event) {
            return true;
        }
    }
    return false;
}

bool RuleEngine::containsState(const StateEvaluator &stateEvaluator, const Event &stateChangeEvent)
{
    if (stateEvaluator.stateDescriptor().isValid() && stateEvaluator.stateDescriptor().stateTypeId().toString() == stateChangeEvent.eventTypeId().toString()) {
        return true;
    }
    foreach (const StateEvaluator &childEvaluator, stateEvaluator.childEvaluators()) {
        if (containsState(childEvaluator, stateChangeEvent)) {
            return true;
        }
    }

    return false;
}

void RuleEngine::appendRule(const Rule &rule)
{
    m_rules.insert(rule.id(), rule);
    m_ruleIds.append(rule.id());
}
