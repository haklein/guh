/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
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
    \page wemo.html
    \title WeMo

    \ingroup plugins
    \ingroup network

    This plugin allows to find and controll devices from WeMo, the
    \l{http://www.belkin.com/de/PRODUKTE/home-automation/c/wemo-home-automation/}{Belkin}
    home automation system.

    \underline{NOTE}: The devices can only be discovered if they are already in the local network. In order
    to configure the WeMo devices please use the original software.

    \chapter Plugin properties
    Following JSON file contains the definition and the description of all available \l{DeviceClass}{DeviceClasses}
    and \l{Vendor}{Vendors} of this \l{DevicePlugin}.

    Each \l{DeviceClass} has a list of \l{ParamType}{paramTypes}, \l{ActionType}{actionTypes}, \l{StateType}{stateTypes}
    and \l{EventType}{eventTypes}. The \l{DeviceClass::CreateMethod}{createMethods} parameter describes how the \l{Device}
    will be created in the system. A device can have more than one \l{DeviceClass::CreateMethod}{CreateMethod}.
    The \l{DeviceClass::SetupMethod}{setupMethod} describes the setup method of the \l{Device}.
    The detailed implementation of each \l{DeviceClass} can be found in the source code.

    \quotefile plugins/deviceplugins/wemo/devicepluginwemo.json
*/

#include "devicepluginwemo.h"

#include "plugin/device.h"
#include "devicemanager.h"
#include "plugininfo.h"

#include <QDebug>

DevicePluginWemo::DevicePluginWemo()
{
    m_discovery = new WemoDiscovery(this);

    connect(m_discovery,SIGNAL(discoveryDone(QList<WemoSwitch*>)),this,SLOT(discoveryDone(QList<WemoSwitch*>)));
}

DeviceManager::DeviceError DevicePluginWemo::discoverDevices(const DeviceClassId &deviceClassId, const ParamList &params)
{
    Q_UNUSED(params)
    if(deviceClassId != wemoSwitchDeviceClassId){
        return DeviceManager::DeviceErrorDeviceClassNotFound;
    }

    m_discovery->discover(2000);

    return DeviceManager::DeviceErrorAsync;
}

DeviceManager::DeviceSetupStatus DevicePluginWemo::setupDevice(Device *device)
{
    if(device->deviceClassId() == wemoSwitchDeviceClassId){
        foreach (WemoSwitch *wemoSwitch, m_wemoSwitches.keys()) {
            if(wemoSwitch->serialNumber() == device->paramValue("serial number").toString()){
                qWarning() << wemoSwitch->serialNumber() << " already exists...";
                return DeviceManager::DeviceSetupStatusFailure;
            }
        }

        device->setName("WeMo Switch (" + device->paramValue("serial number").toString() + ")");

        WemoSwitch *wemoSwitch = new WemoSwitch(this);
        wemoSwitch->setName(device->paramValue("name").toString());
        wemoSwitch->setUuid(device->paramValue("uuid").toString());
        wemoSwitch->setPort(device->paramValue("port").toInt());
        wemoSwitch->setModelName(device->paramValue("model").toString());
        wemoSwitch->setHostAddress(QHostAddress(device->paramValue("host address").toString()));
        wemoSwitch->setModelDescription(device->paramValue("model description").toString());
        wemoSwitch->setSerialNumber(device->paramValue("serial number").toString());
        wemoSwitch->setLocation(QUrl(device->paramValue("location").toString()));
        wemoSwitch->setManufacturer(device->paramValue("manufacturer").toString());
        wemoSwitch->setDeviceType(device->paramValue("device type").toString());

        connect(wemoSwitch,SIGNAL(stateChanged()),this,SLOT(wemoSwitchStateChanged()));
        connect(wemoSwitch,SIGNAL(setPowerFinished(bool,ActionId)),this,SLOT(setPowerFinished(bool,ActionId)));

        m_wemoSwitches.insert(wemoSwitch,device);
        wemoSwitch->refresh();
        return DeviceManager::DeviceSetupStatusSuccess;
    }
    return DeviceManager::DeviceSetupStatusSuccess;
}

DeviceManager::HardwareResources DevicePluginWemo::requiredHardware() const
{
    return DeviceManager::HardwareResourceTimer;
}

DeviceManager::DeviceError DevicePluginWemo::executeAction(Device *device, const Action &action)
{
    if(device->deviceClassId() == wemoSwitchDeviceClassId){
        if(action.actionTypeId() == powerActionTypeId){
            WemoSwitch *wemoSwitch = m_wemoSwitches.key(device);
            wemoSwitch->setPower(action.param("power").value().toBool(),action.id());

            return DeviceManager::DeviceErrorAsync;
        }else{
            return DeviceManager::DeviceErrorActionTypeNotFound;
        }
    }

    return DeviceManager::DeviceErrorDeviceClassNotFound;
}

void DevicePluginWemo::deviceRemoved(Device *device)
{
    if (!m_wemoSwitches.values().contains(device)) {
        return;
    }

    WemoSwitch *wemoSwitch= m_wemoSwitches.key(device);
    qDebug() << "remove wemo swich " << wemoSwitch->serialNumber();
    wemoSwitch->deleteLater();
    m_wemoSwitches.remove(wemoSwitch);
}

void DevicePluginWemo::guhTimer()
{
    foreach (WemoSwitch* wemoSwitch, m_wemoSwitches.keys()) {
        wemoSwitch->refresh();
    }
}

void DevicePluginWemo::discoveryDone(QList<WemoSwitch *> deviceList)
{
    QList<DeviceDescriptor> deviceDescriptors;
    foreach (WemoSwitch *device, deviceList) {
        DeviceDescriptor descriptor(wemoSwitchDeviceClassId, "WeMo Switch", device->serialNumber());
        ParamList params;
        params.append(Param("name", device->name()));
        params.append(Param("uuid", device->uuid()));
        params.append(Param("port", device->port()));
        params.append(Param("model", device->modelName()));
        params.append(Param("model description", device->modelDescription()));
        params.append(Param("serial number", device->serialNumber()));
        params.append(Param("host address", device->hostAddress().toString()));
        params.append(Param("location", device->location().toString()));
        params.append(Param("manufacturer", device->manufacturer()));
        params.append(Param("device type", device->deviceType()));
        descriptor.setParams(params);
        deviceDescriptors.append(descriptor);
    }
    emit devicesDiscovered(wemoSwitchDeviceClassId, deviceDescriptors);
}

void DevicePluginWemo::wemoSwitchStateChanged()
{
    WemoSwitch *wemoSwitch = static_cast<WemoSwitch*>(sender());

    if(m_wemoSwitches.contains(wemoSwitch)){
        Device * device = m_wemoSwitches.value(wemoSwitch);
        device->setStateValue(powerStateTypeId, wemoSwitch->powerState());
        device->setStateValue(reachableStateTypeId, wemoSwitch->reachable());
    }
}

void DevicePluginWemo::setPowerFinished(const bool &succeeded, const ActionId &actionId)
{
    if(succeeded){
        emit actionExecutionFinished(actionId,DeviceManager::DeviceErrorNoError);
    }else{
        emit actionExecutionFinished(actionId,DeviceManager::DeviceErrorHardwareFailure);
    }
}
