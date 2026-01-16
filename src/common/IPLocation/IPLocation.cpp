/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "IPLocation.h"
#include "Config.h"
#include "Errors.h"
#include "IpAddress.h"
#include "Log.h"
#include "StringConvert.h"
#include "Util.h"
#include <fstream>
#include <iostream>
#include <algorithm>

IpLocationStore::IpLocationStore()
{
}

IpLocationStore::~IpLocationStore()
{
}

void IpLocationStore::Load()
{
    _ipLocationStore.clear();
    TC_LOG_INFO("server.loading", "Loading IP Location Database...");

    std::string databaseFilePath = sConfigMgr->GetStringDefault("IPLocationFile", "");
    if (databaseFilePath.empty())
        return;

    std::ifstream databaseFile(databaseFilePath);
    if (!databaseFile.is_open())
    {
        TC_LOG_ERROR("server.loading", "IPLocation: Cannot open ip database file ({}).", databaseFilePath);
        return;
    }

    std::string ipFrom, ipTo, countryCode, countryName;

    while (std::getline(databaseFile, ipFrom, ',') &&
           std::getline(databaseFile, ipTo, ',') &&
           std::getline(databaseFile, countryCode, ',') &&
           std::getline(databaseFile, countryName))
    {
        // Remove carriage returns / line breaks
        countryName.erase(std::remove(countryName.begin(), countryName.end(), '\r'), countryName.end());
        countryName.erase(std::remove(countryName.begin(), countryName.end(), '\n'), countryName.end());

        // Helper lambda to strip quotes
        auto cleanup = [](std::string& str)
        {
            str.erase(std::remove(str.begin(), str.end(), '"'), str.end());
        };

        cleanup(ipFrom);
        cleanup(ipTo);
        cleanup(countryCode);
        cleanup(countryName);

        strToLower(countryCode);

        Optional<uint32> from = Trinity::StringTo<uint32>(ipFrom);
        Optional<uint32> to   = Trinity::StringTo<uint32>(ipTo);

        if (!from || !to || *from > *to)
            continue; // skip invalid or reversed ranges

        _ipLocationStore.emplace_back(*from, *to, std::move(countryCode), std::move(countryName));
    }

    if (_ipLocationStore.empty())
    {
        TC_LOG_WARN("server.loading", "IPLocation: No valid entries loaded from ({}).", databaseFilePath);
        return;
    }

    // Sort ranges by IpFrom
    std::sort(_ipLocationStore.begin(), _ipLocationStore.end(),
        [](IpLocationRecord const& a, IpLocationRecord const& b) { return a.IpFrom < b.IpFrom; });

    // Detect overlapping or touching ranges
    for (size_t i = 1; i < _ipLocationStore.size(); ++i)
    {
        if (_ipLocationStore[i - 1].IpTo >= _ipLocationStore[i].IpFrom)
        {
            TC_LOG_ERROR("server.loading",
                "Overlapping IP ranges detected: {}-{} overlaps with {}-{}",
                _ipLocationStore[i - 1].IpFrom, _ipLocationStore[i - 1].IpTo,
                _ipLocationStore[i].IpFrom, _ipLocationStore[i].IpTo);
            ASSERT(false, "Overlapping IP ranges detected in database file");
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} ip location entries.", _ipLocationStore.size());
}

IpLocationRecord const* IpLocationStore::GetLocationRecord(std::string const& ipAddress) const
{
    boost::system::error_code error;
    boost::asio::ip::address_v4 address = Trinity::Net::make_address_v4(ipAddress, error);
    if (error)
        return nullptr;

    uint32 ip = Trinity::Net::address_to_uint(address);

    // Find the first record where IpFrom <= ip
    auto itr = std::lower_bound(_ipLocationStore.begin(), _ipLocationStore.end(), ip,
        [](IpLocationRecord const& loc, uint32 ip) { return loc.IpTo < ip; });

    if (itr == _ipLocationStore.end())
        return nullptr;

    if (ip < itr->IpFrom || ip > itr->IpTo)
        return nullptr;

    return &(*itr);
}

IpLocationStore* IpLocationStore::Instance()
{
    static IpLocationStore instance;
    return &instance;
}
