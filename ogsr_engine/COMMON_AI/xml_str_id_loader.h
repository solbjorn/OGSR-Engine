#pragma once

#include "../xrGame/ui/xrUIXmlParser.h"

#ifndef XRGAME_EXPORTS
#include "object_broker.h"
#endif // XRGAME_EXPORTS

// T_INIT -  класс где определена статическая InitXmlIdToIndex
//           функция инициализации file_str и tag_name

// структура хранит строковый id элемента
// файл и позицию, где этот элемент находится
struct ITEM_DATA final
{
    shared_str id;
    int index;
    int pos_in_file;
    CUIXml* _xml;

    constexpr ITEM_DATA() = default;
    constexpr explicit ITEM_DATA(gsl::czstring str, int idx, int pos, CUIXml* xml) : index{idx}, pos_in_file{pos}, _xml{xml} { id._set(str); }
};
using T_VECTOR = xr_vector<ITEM_DATA>;

void _destroy_item_data_vector_cont(T_VECTOR* vec);

#define TEMPLATE_SPECIALIZATION template <typename T_INIT>
#define CSXML_IdToIndex CXML_IdToIndex<T_INIT>

TEMPLATE_SPECIALIZATION
class CXML_IdToIndex : public virtual RTTI::Enable
{
    RTTI_DECLARE_TYPEINFO(CXML_IdToIndex<T_INIT>);

private:
    static T_VECTOR* m_pItemDataVector;

protected:
    // имена xml файлов (разделенных запятой) из которых
    // производить загрузку элементов
    static LPCSTR file_str;
    // имена тегов
    static LPCSTR tag_name;

public:
    CXML_IdToIndex() = default;
    ~CXML_IdToIndex() override = default;

    static void InitInternal();

    static const ITEM_DATA* GetById(const shared_str& str_id, bool no_assert = false);
    static const ITEM_DATA* GetByIndex(int index, bool no_assert = false);

    static int IdToIndex(const shared_str& str_id, int default_index = -1, bool no_assert = false)
    {
        const ITEM_DATA* item = GetById(str_id, no_assert);
        return item ? item->index : default_index;
    }

    static const shared_str IndexToId(int index, shared_str default_id = {}, bool no_assert = false)
    {
        const ITEM_DATA* item = GetByIndex(index, no_assert);
        return item ? item->id : default_id;
    }

    static int GetMaxIndex() { return m_pItemDataVector->size() - 1; }

    // удаление статичекого массива
    static void DeleteIdToIndexData();
};

TEMPLATE_SPECIALIZATION
T_VECTOR* CSXML_IdToIndex::m_pItemDataVector{};

TEMPLATE_SPECIALIZATION
LPCSTR CSXML_IdToIndex::file_str{};

TEMPLATE_SPECIALIZATION
LPCSTR CSXML_IdToIndex::tag_name{};

TEMPLATE_SPECIALIZATION
const ITEM_DATA* CSXML_IdToIndex::GetById(const shared_str& str_id, bool no_assert)
{
    T_INIT::InitXmlIdToIndex();

    auto it = m_pItemDataVector->begin();
    for (; m_pItemDataVector->end() != it; it++)
    {
        if ((*it).id == str_id)
            break;
    }

    if (m_pItemDataVector->end() == it)
    {
        int i = 0;
        for (T_VECTOR::iterator it = m_pItemDataVector->begin(); m_pItemDataVector->end() != it; it++, i++)
            XR_LOG_ERROR("[{}]=[{}]", i, it->id);

        XR_ASSERT(no_assert, "item not found", file_str, str_id);
        return nullptr;
    }

    return &(*it);
}

TEMPLATE_SPECIALIZATION
const ITEM_DATA* CSXML_IdToIndex::GetByIndex(int index, bool no_assert)
{
    if (index >= std::ssize(*m_pItemDataVector))
    {
        XR_ASSERT(no_assert, "item not found by index", file_str, index, m_pItemDataVector->size());
        return nullptr;
    }

    return &(*m_pItemDataVector)[index];
}

TEMPLATE_SPECIALIZATION
void CSXML_IdToIndex::DeleteIdToIndexData()
{
    XR_ASSERT(m_pItemDataVector != nullptr);

    _destroy_item_data_vector_cont(m_pItemDataVector);
    xr_delete(m_pItemDataVector);
}

TEMPLATE_SPECIALIZATION
void CSXML_IdToIndex::InitInternal()
{
    XR_ASSERT(m_pItemDataVector == nullptr);
    T_INIT::InitXmlIdToIndex();

    m_pItemDataVector = xr_new<T_VECTOR>();
    XR_ASSERT(file_str != nullptr && tag_name != nullptr);

    string_path xml_file;
    int count = _GetItemCount(file_str);
    int index = 0;
    for (int it = 0; it < count; ++it)
    {
        std::ignore = _GetItem(file_str, it, xml_file);

        auto uiXml = xr_new<CUIXml>();
        const auto xml_file_full = xr::format("{}.xml", xml_file);
        XR_ASSERT(uiXml->Init(CONFIG_PATH, GAME_PATH, xml_file_full.c_str()), "error parsing XML", xml_file_full, tag_name);

        // общий список
        int items_num = uiXml->GetNodesNum(uiXml->GetRoot(), tag_name);

        for (int i = 0; i < items_num; ++i)
        {
            const auto item_name =
                XR_ASSERT_VAL(uiXml->ReadAttrib(uiXml->GetRoot(), tag_name, i, "id", nullptr) != nullptr, "ID is not set for item", xml_file, tag_name, i);

            // проверить ID на уникальность
            for (const auto& item : *m_pItemDataVector)
                XR_ASSERT(std::is_neq(xr_strcmp(item.id, item_name)), "duplicate item ID", xml_file, tag_name, item_name);

            m_pItemDataVector->emplace_back(item_name, index, i, uiXml);
            ++index;
        }

        if (!items_num)
            xr_delete(uiXml);
    }
}

#undef TEMPLATE_SPECIALIZATION
