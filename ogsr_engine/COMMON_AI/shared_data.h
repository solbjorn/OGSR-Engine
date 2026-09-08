#pragma once

// Singleton template definition
template <class T>
class CSingleton : public virtual RTTI::Enable
{
    RTTI_DECLARE_TYPEINFO(CSingleton<T>);

private:
    static T* _self;
    static int _refcount;

public:
    // whether singleton will delete itself on FreeInst
    // when _refcount = 0
    // otherwise user should call DestroySingleton() manually
    static bool _on_self_delete;

public:
    CSingleton() = default;
    ~CSingleton() override { _self = nullptr; }

    static void DestroySingleton()
    {
        if (!_self)
            return;

        XR_LOG_INFO("DestroySingleton::RefCounter: [{}]", _refcount);

        XR_ASSERT(_on_self_delete == false);
        XR_ASSERT(_refcount == 0);

        xr_delete(_self);
    }

public:
    static T* Instance()
    {
        if (!_self)
            _self = xr_new<T>();
        ++_refcount;
        return _self;
    }

    void FreeInst()
    {
        if (0 == --_refcount)
        {
            if (_on_self_delete)
            {
                CSingleton<T>* ptr = this;
                xr_delete(ptr);
            }
        }
    }
};

template <class T>
T* CSingleton<T>::_self{};

template <class T>
int CSingleton<T>::_refcount{};

template <class T>
bool CSingleton<T>::_on_self_delete{true};

template <class SHARED_TYPE, class KEY_TYPE>
class CSharedObj final : public CSingleton<CSharedObj<SHARED_TYPE, KEY_TYPE>>
{
    RTTI_DECLARE_TYPEINFO(CSharedObj<SHARED_TYPE, KEY_TYPE>, CSingleton<CSharedObj<SHARED_TYPE, KEY_TYPE>>);

public:
    xr_map<KEY_TYPE, SHARED_TYPE*> _shared_tab;
    typedef typename xr_map<KEY_TYPE, SHARED_TYPE*>::iterator SHARED_DATA_MAP_IT;

    CSharedObj() = default;

    ~CSharedObj() override
    {
        for (auto& it : _shared_tab)
            xr_delete(it.second);
    }

    // Access to data
    SHARED_TYPE* get_shared(KEY_TYPE id)
    {
        SHARED_DATA_MAP_IT shared_it = _shared_tab.find(id);
        SHARED_TYPE* _data;

        // if not found - create appropriate shared data object
        if (_shared_tab.end() == shared_it)
        {
            _data = xr_new<SHARED_TYPE>();
            _shared_tab.try_emplace(id, _data);
        }
        else
        {
            _data = shared_it->second;
        }

        return _data;
    }
};

class CSharedResource : public virtual RTTI::Enable
{
    RTTI_DECLARE_TYPEINFO(CSharedResource);

public:
    bool loaded{};

    CSharedResource() = default;
    ~CSharedResource() override = default;

    bool IsLoaded() { return loaded; }
    void SetLoad(bool l = true) { loaded = l; }
};

template <class SHARED_TYPE, class KEY_TYPE, bool auto_delete = true>
class CSharedClass : public virtual RTTI::Enable
{
    RTTI_DECLARE_TYPEINFO(CSharedClass<SHARED_TYPE, KEY_TYPE, auto_delete>);

public:
    SHARED_TYPE* _sd{};
    CSharedObj<SHARED_TYPE, KEY_TYPE>* pSharedObj;

    CSharedClass()
    {
        pSharedObj = CSharedObj<SHARED_TYPE, KEY_TYPE>::Instance();
        pSharedObj->_on_self_delete = auto_delete;
    }

    ~CSharedClass() override { pSharedObj->FreeInst(); }

    static void DeleteSharedData() { CSharedObj<SHARED_TYPE, KEY_TYPE>::DestroySingleton(); }

    void load_shared(KEY_TYPE key, LPCSTR section)
    {
        _sd = pSharedObj->get_shared(key);

        if (!get_sd()->IsLoaded())
        {
            load_shared(section);
            get_sd()->SetLoad();
        }
    }

    virtual void load_shared(gsl::czstring) {}

    SHARED_TYPE* get_sd() { return _sd; }
    const SHARED_TYPE* get_sd() const { return _sd; }

    // управление загрузкой данных при компонентном подходе (загрузка данных вручную)
    bool start_load_shared(KEY_TYPE key)
    {
        _sd = pSharedObj->get_shared(key);
        if (get_sd()->IsLoaded())
            return false;
        return true;
    }

    void finish_load_shared() { get_sd()->SetLoad(); }
};
