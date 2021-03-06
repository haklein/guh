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

#ifndef TRIGGERTYPE_H
#define TRIGGERTYPE_H

#include "typeutils.h"
#include "paramtype.h"

#include <QVariantMap>

class EventType
{
public:
    EventType(const EventTypeId &id);

    EventTypeId id() const;

    QString name() const;
    void setName(const QString &name);

    QList<ParamType> paramTypes() const;
    void setParamTypes(const QList<ParamType> &paramTypes);

private:
    EventTypeId m_id;
    QString m_name;

    QList<ParamType> m_paramTypes;

};

#endif // TRIGGERTYPE_H
