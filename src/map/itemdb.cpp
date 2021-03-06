#include "itemdb.hpp"
//    itemdb.cpp - Item definitions.
//
//    Copyright © ????-2004 Athena Dev Teams
//    Copyright © 2004-2011 The Mana World Development Team
//    Copyright © 2011-2014 Ben Longbons <b.r.longbons@gmail.com>
//
//    This file is part of The Mana World (Athena server)
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <algorithm>

#include "../strings/astring.hpp"
#include "../strings/zstring.hpp"
#include "../strings/xstring.hpp"

#include "../generic/db.hpp"

#include "../io/cxxstdio.hpp"
#include "../io/extract.hpp"
#include "../io/read.hpp"

#include "../mmo/config_parse.hpp"
#include "../mmo/extract_enums.hpp"

#include "script-parse.hpp"

#include "../poison.hpp"


namespace tmwa
{
static
Map<ItemNameId, struct item_data> item_db;

// Function declarations

/*==========================================
 * 名前で検索用
 *------------------------------------------
 */
// name = item alias, so we should find items aliases first. if not found then look for "jname" (full name)
static
void itemdb_searchname_sub(Borrowed<struct item_data> item, ItemName str, Borrowed<Option<Borrowed<struct item_data>>> dst)
{
    if (item->name == str)
        *dst = Some(item);
}

/*==========================================
 * 名前で検索
 *------------------------------------------
 */
Option<Borrowed<struct item_data>> itemdb_searchname(XString str_)
{
    ItemName str = stringish<ItemName>(str_);
    if (XString(str) != str_)
        return None;
    Option<P<struct item_data>> item = None;
    for (auto& pair : item_db)
        itemdb_searchname_sub(borrow(pair.second), str, borrow(item));
    return item;
}

/*==========================================
 * DBの存在確認
 *------------------------------------------
 */
Option<Borrowed<struct item_data>> itemdb_exists(ItemNameId nameid)
{
    return item_db.search(nameid);
}

/*==========================================
 * DBの検索
 *------------------------------------------
 */
Borrowed<struct item_data> itemdb_search(ItemNameId nameid)
{
    Option<P<struct item_data>> id_ = item_db.search(nameid);
    if OPTION_IS_SOME(id, id_)
        return id;

    P<struct item_data> id = item_db.init(nameid);

    id->nameid = nameid;
    id->value_buy = 10;
    id->value_sell = id->value_buy / 2;
    id->weight = 10;
    id->sex = SEX::NEUTRAL;
    id->elv = 0;

    id->type = ItemType::JUNK;

    return id;
}

/*==========================================
 *
 *------------------------------------------
 */
int itemdb_isequip(ItemNameId nameid)
{
    ItemType type = itemdb_type(nameid);
    return !(type == ItemType::USE
        || type == ItemType::_2
        || type == ItemType::JUNK
        || type == ItemType::_6
        || type == ItemType::ARROW);
}

/*==========================================
 *
 *------------------------------------------
 */
bool itemdb_isequip2(Borrowed<struct item_data> data)
{
    ItemType type = data->type;
    return !(type == ItemType::USE
        || type == ItemType::_2
        || type == ItemType::JUNK
        || type == ItemType::_6
        || type == ItemType::ARROW);
}

/*==========================================
 *
 *------------------------------------------
 */
int itemdb_isequip3(ItemNameId nameid)
{
    ItemType type = itemdb_type(nameid);
    return (type == ItemType::WEAPON
        || type == ItemType::ARMOR
        || type == ItemType::_8);
}

bool itemdb_readdb(ZString filename)
{
    bool rv = true;

    int ln = 0, lines = 0;

    {
        io::ReadFile in(filename);

        if (!in.is_open())
        {
            PRINTF("can't read %s\n"_fmt, filename);
            return false;
        }

        lines = 0;

        AString line;
        while (in.getline(line))
        {
            lines++;
            if (is_comment(line))
                continue;
            // a line is 17 normal fields followed by 2 {} fields
            // the fields are separated by ", *", but there may be ,
            // in the {}.

            auto it = std::find(line.begin(), line.end(), '{');
            XString main_part = line.xislice_h(it).rstrip();
            // According to the code, tail_part may be empty. See later.
            ZString tail_part = line.xislice_t(it);

            XString unused_slot_count;
            item_data idv {};
            if (!extract(
                        main_part, record<','>(
                            &idv.nameid,
                            lstripping(&idv.name),
                            lstripping(&idv.jname),
                            lstripping(&idv.type),
                            lstripping(&idv.value_buy),
                            lstripping(&idv.value_sell),
                            lstripping(&idv.weight),
                            lstripping(&idv.atk),
                            lstripping(&idv.def),
                            lstripping(&idv.range),
                            lstripping(&idv.magic_bonus),
                            lstripping(&unused_slot_count),
                            lstripping(&idv.sex),
                            lstripping(&idv.equip),
                            lstripping(&idv.wlv),
                            lstripping(&idv.elv),
                            lstripping(&idv.look)
                        )
                    )
            )
            {
                PRINTF("%s:%d: error: bad item line: %s\n"_fmt,
                        filename, lines, line);
                rv = false;
                continue;
            }

            ln++;

            Borrowed<struct item_data> id = itemdb_search(idv.nameid);
            *id = std::move(idv);
            if (id->value_buy == 0 && id->value_sell == 0)
            {
            }
            else if (id->value_buy == 0)
            {
                id->value_buy = id->value_sell * 2;
            }
            else if (id->value_sell == 0)
            {
                id->value_sell = id->value_buy / 2;
            }

            id->use_script = nullptr;
            id->equip_script = nullptr;

            if (!tail_part)
                continue;
            id->use_script = parse_script(tail_part, lines, true);

            tail_part = tail_part.xislice_t(std::find(tail_part.begin() + 1, tail_part.end(), '{'));
            if (!tail_part)
                continue;
            id->equip_script = parse_script(tail_part, lines, true);
        }
        PRINTF("read %s done (count=%d)\n"_fmt, filename, ln);
    }

    return rv;
}

/*==========================================
 *
 *------------------------------------------
 */
static
void itemdb_final(struct item_data *id)
{
    id->use_script.reset();
    id->equip_script.reset();
}

/*==========================================
 *
 *------------------------------------------
 */
void do_final_itemdb(void)
{
    for (auto& pair : item_db)
        itemdb_final(&pair.second);
    item_db.clear();
}
} // namespace tmwa
