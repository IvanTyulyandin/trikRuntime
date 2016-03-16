/* Copyright 2013 - 2015 Yurii Litvinov, Anastasiia Kornilova and CyberTech Labs Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#include "powerMotor.h"

#include <trikKernel/configurer.h>
#include <trikKernel/exceptions/malformedConfigException.h>
#include <trikKernel/exceptions/internalErrorException.h>

#include "mspI2cCommunicator.h"
#include "configurerHelper.h"

using namespace trikControl;

static const int maxControlValue = 100;
static const int minControlValue = -100;

PowerMotor::PowerMotor(const QString &port, const trikKernel::Configurer &configurer
		, MspCommunicatorInterface &communicator)
	: mCommunicator(communicator)
	, mInvert(configurer.attributeByPort(port, "invert") == "false")
	, mCurrentPower(0)
	, mState("Power Motor on" + port)
{
	mMspCommandNumber = ConfigurerHelper::configureInt(configurer, mState, port, "i2cCommandNumber");

	mCurrentPeriod = ConfigurerHelper::configureInt(configurer, mState, port, "period");
	setPeriod(mCurrentPeriod);

	mPowerMap.reserve(maxControlValue + 1);
	lineariseMotor(port, configurer);

	mState.ready();
}

PowerMotor::~PowerMotor()
{
}

PowerMotor::Status PowerMotor::status() const
{
	return combine(mCommunicator, mState.status());
}

void PowerMotor::setPower(int power, bool constrain)
{
	if (!constrain) {
		throw trikKernel::InternalErrorException("Invalid argument");
	}

	mCurrentPower = power;

	power = power <= 0 ? -mPowerMap[-power] : mPowerMap[power];

	power = mInvert ? -power : power;

	QByteArray command(3, '\0');
	command[0] = static_cast<char>(mMspCommandNumber & 0xFF);
	command[1] = static_cast<char>((mMspCommandNumber >> 8) & 0xFF);
	command[2] = static_cast<char>(power & 0xFF);

	mCommunicator.send(command);
}

int PowerMotor::power() const
{
	return mCurrentPower;
}

int PowerMotor::period() const
{
	return mCurrentPeriod;
}

void PowerMotor::powerOff()
{
	setPower(0);
}

void PowerMotor::setPeriod(int period)
{
	mCurrentPeriod = period;
	QByteArray command(4, '\0');
	command[0] = static_cast<char>((mMspCommandNumber - 4) & 0xFF);
	command[2] = static_cast<char>(period & 0xFF);
	command[3] = static_cast<char>(period >> 8);
	mCommunicator.send(command);
}

void PowerMotor::lineariseMotor(const QString &port, const trikKernel::Configurer &configurer)
{
	QVector<QPair<double, double> > powerAddiction;
	for (const QString &str : configurer.attributeByPort(port, "measures").split(")")) {
		if (str != "") {
			QPair<double, double> temp;
			temp.first = str.mid(1).split(";").at(0).toInt();
			temp.second = str.mid(1).split(";").at(1).toInt();
			powerAddiction.append(temp);
		}
	}

	const int addictionLength = powerAddiction.size();
	const int maxValue = powerAddiction[addictionLength - 1].second;

	for (int i = 0; i < addictionLength; i++) {
		powerAddiction[i].second *= maxControlValue;
		powerAddiction[i].second /= maxValue;
	}

	for (int i = 0; i < maxControlValue; i++)
	{
		int k = 0;
		while (i >= powerAddiction[k].second) {
			k++;
		}
		k--;

		const double measureDifference = powerAddiction[k+1].second - powerAddiction[k].second;
		const double axeDifferenece = powerAddiction[k+1].first - powerAddiction[k].first;

		if (measureDifference < 0 || axeDifferenece < 0) {
			throw trikKernel::MalformedConfigException("Nonmonotonic function");
		}

		const double koef = axeDifferenece / measureDifference;
		const int power = powerAddiction[k].first + koef * (i - powerAddiction[k].second);
		mPowerMap.append(power);
	}
	mPowerMap.append(maxControlValue);
}

int PowerMotor::minControl() const
{
	return minControlValue;
}

int PowerMotor::maxControl() const
{
	return maxControlValue;
}
