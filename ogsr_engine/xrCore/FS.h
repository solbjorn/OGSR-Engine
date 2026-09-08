// FS.h: interface for the CFS class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

constexpr inline size_t CFS_CompressMark{1ul << 31ul};
constexpr inline size_t CFS_HeaderChunkID{666};

void VerifyPath(std::string_view path);

#ifdef DEBUG
extern u32 g_file_mapped_memory;
extern u32 g_file_mapped_count;
void dump_file_mappings();
extern void register_file_mapping(void* address, const u32& size, LPCSTR file_name);
extern void unregister_file_mapping(void* address, const u32& size);
#endif // DEBUG

//------------------------------------------------------------------------------------
// Write
//------------------------------------------------------------------------------------
class XR_NOVTABLE IWriter : public virtual RTTI::Enable
{
    RTTI_DECLARE_TYPEINFO(IWriter);

private:
    xr_stack<gsl::index> chunk_pos;

public:
    shared_str fName;

public:
    IWriter() = default;
    ~IWriter() override { XR_ASSERT(chunk_pos.size() == 0, "opened chunk not closed", fName, chunk_pos); }

    // kernel
    virtual void seek(gsl::index pos) = 0;
    [[nodiscard]] virtual gsl::index tell() = 0;

    virtual void w(const void* ptr, gsl::index count) = 0;

    // generalized writing functions
    IC void w_u64(u64 d) { w(&d, sizeof(u64)); }
    IC void w_u32(u32 d) { w(&d, sizeof(u32)); }
    IC void w_u16(u16 d) { w(&d, sizeof(u16)); }
    IC void w_u8(u8 d) { w(&d, sizeof(u8)); }
    IC void w_s64(s64 d) { w(&d, sizeof(s64)); }
    IC void w_s32(s32 d) { w(&d, sizeof(s32)); }
    IC void w_s16(s16 d) { w(&d, sizeof(s16)); }
    IC void w_s8(s8 d) { w(&d, sizeof(s8)); }
    IC void w_float(float d) { w(&d, sizeof(float)); }

    void w_string(gsl::czstring p)
    {
        w(p, xr_strlen(p));
        w_u8(13);
        w_u8(10);
    }

    void w_stringZ(gsl::czstring p) { w(p, xr_strlen(p) + 1); }

    void w_stringZ(const shared_str& p)
    {
        w(p.data(), p.size());
        w_u8(0);
    }

    void w_stringZ(const xr_string& p)
    {
        w(p.data(), std::ssize(p));
        w_u8(0);
    }

    IC void w_fcolor(const Fcolor& v) { w(&v, sizeof(Fcolor)); }
    void w_fvector4(const Fvector4& v) { w(&v, sizeof(Fvector4)); }
    void w_fvector3(const Fvector3& v) { w(&v, sizeof(Fvector3)); }
    IC void w_fvector2(const Fvector2& v) { w(&v, sizeof(Fvector2)); }
    void w_ivector4(const Ivector4& v) { w(&v, sizeof(Ivector4)); }
    void w_ivector3(const Ivector3& v) { w(&v, sizeof(Ivector3)); }
    IC void w_ivector2(const Ivector2& v) { w(&v, sizeof(Ivector2)); }

    // quant writing functions
    IC void w_float_q16(float a, float min, float max)
    {
        XR_ASSERT(a >= min && a <= max, "", a, min, max);

        float q = (a - min) / (max - min);
        w_u16(u16(iFloor(q * 65535.f + .5f)));
    }

    IC void w_float_q8(float a, float min, float max)
    {
        XR_ASSERT(a >= min && a <= max, "", a, min, max);

        float q = (a - min) / (max - min);
        w_u8(u8(iFloor(q * 255.f + .5f)));
    }

    IC void w_angle16(float a) { w_float_q16(angle_normalize(a), 0, PI_MUL_2); }
    IC void w_angle8(float a) { w_float_q8(angle_normalize(a), 0, PI_MUL_2); }
    IC void w_dir(const Fvector& D) { w_u16(pvCompress(D)); }

    void w_sdir(const Fvector& D);

private:
    void w_vprintf(xr::detail::string_view fmt, xr::detail::format_args args);

public:
    template <typename... Args>
    void w_printf(xr::detail::format_string<Args...> fmt, Args&&... args)
    {
        w_vprintf(fmt.get(), xr::detail::make_format_args(args...));
    }

    // generalized chunking
    void open_chunk(u32 type);
    void close_chunk();
    [[nodiscard]] gsl::index chunk_size(); // returns size of currently opened chunk, 0 otherwise
    void w_compressed(void* ptr, gsl::index count, bool encrypt = false, bool is_ww = false);
    void w_chunk(u32 type, void* data, gsl::index size, bool encrypt = false, bool is_ww = false);
    [[nodiscard]] virtual bool valid() { return true; }
    [[nodiscard]] virtual gsl::index flush() { return 0; } // RvP
};

class CMemoryWriter final : public IWriter
{
    RTTI_DECLARE_TYPEINFO(CMemoryWriter, IWriter);

public:
    std::byte* data{};
    gsl::index position{};
    gsl::index mem_size{};
    gsl::index file_size{};

    CMemoryWriter() = default;
    ~CMemoryWriter() override;

    // kernel
    void w(const void* ptr, gsl::index count) override;

    void seek(gsl::index pos) override { position = pos; }
    [[nodiscard]] gsl::index tell() override { return position; }

    // specific
    [[nodiscard]] std::byte* pointer() const { return data; }
    [[nodiscard]] gsl::index size() const { return file_size; }

    IC void clear()
    {
        file_size = 0;
        position = 0;
    }

    IC void free()
    {
        file_size = 0;
        position = 0;
        mem_size = 0;
        xr_free(data);
    }

    [[nodiscard]] bool save_to(LPCSTR fn) const;
    void reserve(gsl::index count);
};

//------------------------------------------------------------------------------------
// Read
//------------------------------------------------------------------------------------

template <typename implementation_type>
class IReaderBase : public virtual RTTI::Enable
{
    RTTI_DECLARE_TYPEINFO(IReaderBase<implementation_type>);

public:
    ~IReaderBase() override = default;

    [[nodiscard]] implementation_type& impl() { return *smart_cast<implementation_type*>(this); }
    [[nodiscard]] const implementation_type& impl() const { return *smart_cast<const implementation_type*>(this); }

    [[nodiscard]] bool eof() const { return impl().elapsed() <= 0; }

    IC void r(void* p, gsl::index cnt) { impl().r(p, cnt); }

    [[nodiscard]] virtual Fvector r_vec3()
    {
        Fvector tmp;
        r(&tmp, 3 * sizeof(float));
        return tmp;
    }

    [[nodiscard]] virtual Fvector4 r_vec4()
    {
        Fvector4 tmp;
        r(&tmp, 4 * sizeof(float));
        return tmp;
    }

    [[nodiscard]] virtual u64 r_u64()
    {
        u64 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual u32 r_u32()
    {
        u32 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual u16 r_u16()
    {
        u16 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual u8 r_u8()
    {
        u8 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual s64 r_s64()
    {
        s64 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual s32 r_s32()
    {
        s32 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual s16 r_s16()
    {
        s16 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual s8 r_s8()
    {
        s8 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] virtual float r_float()
    {
        f32 tmp;
        r(&tmp, sizeof(tmp));
        return tmp;
    }

    virtual void r_fvector4(Fvector4& v) { r(&v, sizeof(Fvector4)); }
    virtual void r_fvector3(Fvector3& v) { r(&v, sizeof(Fvector3)); }
    virtual void r_fvector2(Fvector2& v) { r(&v, sizeof(Fvector2)); }
    virtual void r_ivector4(Ivector4& v) { r(&v, sizeof(Ivector4)); }
    virtual void r_ivector3(Ivector3& v) { r(&v, sizeof(Ivector3)); }
    virtual void r_ivector2(Ivector2& v) { r(&v, sizeof(Ivector2)); }
    virtual void r_fcolor(Fcolor& v) { r(&v, sizeof(Fcolor)); }

    [[nodiscard]] virtual float r_float_q16(float min, float max)
    {
        u16 val = r_u16();
        float A = (float(val) * (max - min)) / 65535.f + min; // floating-point-error possible
        XR_ASSERT(A >= min - EPS_S && A <= max + EPS_S, "", A, min, max);

        return A;
    }

    [[nodiscard]] virtual float r_float_q8(float min, float max)
    {
        u8 val = r_u8();
        float A = (float(val) / 255.0001f) * (max - min) + min; // floating-point-error possible
        XR_ASSERT(A >= min && A <= max, "", A, min, max);

        return A;
    }

    [[nodiscard]] f32 r_angle16() { return r_float_q16(0, PI_MUL_2); }
    [[nodiscard]] f32 r_angle8() { return r_float_q8(0, PI_MUL_2); }

    virtual void r_dir(Fvector& A)
    {
        u16 t = r_u16();
        pvDecompress(A, t);
    }

    virtual void r_sdir(Fvector& A)
    {
        u16 t = r_u16();
        float s = r_float();
        pvDecompress(A, t);
        A.mul(s);
    }

    // Set file pointer to start of chunk data (0 for root chunk)
    IC void rewind() { impl().seek(0); }

    [[nodiscard]] gsl::index find_chunk(u32 ID, BOOL* bCompressed = nullptr)
    {
        gsl::index dwSize{};
        size_t dwType{};
        bool success{};

        if (m_last_pos != 0)
        {
            impl().seek(m_last_pos);
            if (impl().elapsed() >= gsl::index{sizeof(u32) * 2})
            {
                dwType = r_u32();
                dwSize = r_u32();

                if ((dwType & ~CFS_CompressMark) == ID)
                    success = true;
            }
        }

        if (!success)
        {
            rewind();
            while (impl().elapsed() >= gsl::index{sizeof(u32) * 2}) // while (!eof())
            {
                dwType = r_u32();
                dwSize = r_u32();
                if ((dwType & ~CFS_CompressMark) == ID)
                {
                    success = true;
                    break;
                }
                else
                {
                    if (impl().elapsed() > dwSize)
                        impl().advance(dwSize);
                    else
                        break;
                }
            }

            if (!success)
            {
                m_last_pos = 0;
                return 0;
            }
        }

        if (bCompressed != nullptr)
            *bCompressed = !!(dwType & CFS_CompressMark);

        // Встречаются объекты, в которых dwSize последнего чанка больше чем реальный размер чанка который там есть.
        // К примеру, в одной из моделей гранат получается превышение на 9 байт.
        // Не знаю, от чего такое бывает, но попробуем обработать эту ситуацию.
        if (impl().elapsed() >= dwSize)
        {
            m_last_pos = impl().tell() + dwSize;
            return dwSize;
        }
        else
        {
            XR_LOG_ERROR("Chunk [{}] has invalid size [{}], return elapsed size [{}]", ID, dwSize, impl().elapsed());

            m_last_pos = 0;
            return impl().elapsed();
        }
    }

    [[nodiscard]] gsl::index find_chunk_thm(u32 ID, const char* dbg_name)
    {
        gsl::index dwSize{};
        size_t dwType{};
        bool success{};

        if (m_last_pos != 0)
        {
            impl().seek(m_last_pos);
            if (impl().elapsed() >= gsl::index{sizeof(u32) * 2})
            {
                dwType = r_u32();
                dwSize = r_u32();
                if ((dwType & ~CFS_CompressMark) == ID)
                    success = true;
            }
        }

        if (!success)
        {
            rewind();
            while (impl().elapsed() >= gsl::index{sizeof(u32) * 2}) // while (!eof())
            {
                dwType = r_u32();
                dwSize = r_u32();
                if ((dwType & ~CFS_CompressMark) == ID)
                {
                    success = true;
                    break;
                }
                else
                {
                    if ((ID & 0x7ffffff0) == 0x810) // is it a thm chunk ID?
                    {
                        const auto pos = impl().tell();
                        const auto size = impl().length();
                        auto length = dwSize;

                        if (pos + length != size) // not the last chunk in the file?
                        {
                            bool ok = true;
                            if (pos + length > size - 8)
                                ok = false; // size too large?
                            if (ok)
                            {
                                impl().seek(pos + length);
                                if ((r_u32() & 0x7ffffff0) != 0x810)
                                    ok = false; // size too small?
                            }
                            if (!ok) // size incorrect?
                            {
                                length = 0;
                                while (pos + length < size) // find correct size, up to eof
                                {
                                    impl().seek(pos + length);
                                    if (pos + length <= size - 8 && (r_u32() & 0x7ffffff0) == 0x810)
                                        break; // found start of next section
                                    length++;
                                }

                                XR_LOG_ERROR("THM [{}] chunk [{}] fixed, wrong size = [{}], correct size = [{}]", dbg_name, ID, dwSize, length);
                            }
                        }

                        impl().seek(pos); // go back to beginning of chunk
                        dwSize = length; // use correct(ed) size
                    }
                    impl().advance(dwSize);
                }
            }

            if (!success)
            {
                m_last_pos = 0;
                return 0;
            }
        }

        // см. комментарии выше в функции find_chunk
        if (impl().elapsed() >= dwSize)
        {
            m_last_pos = impl().tell() + dwSize;
            return dwSize;
        }
        else
        {
            XR_LOG_ERROR("Chunk [{}] has invalid size [{}], return elapsed size [{}]", ID, dwSize, impl().elapsed());

            m_last_pos = 0;
            return impl().elapsed();
        }
    }

    [[nodiscard]] BOOL r_chunk(u32 ID, void* dest) // чтение XR Chunk'ов (4b-ID,4b-size,??b-data)
    {
        gsl::index dwSize = find_chunk(ID);
        if (dwSize != 0)
        {
            r(dest, dwSize);
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }

    [[nodiscard]] BOOL r_chunk_safe(u32 ID, void* dest, gsl::index dest_size) // чтение XR Chunk'ов (4b-ID,4b-size,??b-data)
    {
        gsl::index dwSize = find_chunk(ID);
        if (dwSize != 0)
        {
            XR_ASSERT(dwSize == dest_size, "", ID);
            r(dest, dwSize);

            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }

private:
    gsl::index m_last_pos{};
};

class IReader : public IReaderBase<IReader>
{
    RTTI_DECLARE_TYPEINFO(IReader, IReaderBase<IReader>);

protected:
    const std::byte* data{};
    gsl::index Pos{};
    gsl::index Size{};
    gsl::index iterpos{};

public:
    IReader() = default;

    explicit IReader(const void* _data, gsl::index _size, gsl::index _iterpos = 0)
    {
        data = static_cast<const std::byte*>(_data);
        Size = _size;
        iterpos = _iterpos;
    }

    ~IReader() override = default;

protected:
    [[nodiscard]] gsl::index advance_term_string();

public:
    [[nodiscard]] gsl::index elapsed() const { return Size - Pos; }
    [[nodiscard]] gsl::index tell() const { return Pos; }

    void seek(gsl::index ptr)
    {
        Pos = ptr;
        XR_ASSERT(Pos >= 0 && Pos <= Size, "", Pos, Size);
    }

    [[nodiscard]] gsl::index length() const { return Size; }

    [[nodiscard]] const void* pointer() const { return &(data[Pos]); }
    [[nodiscard]] const void* begin() const { return data; }
    [[nodiscard]] const void* end() const { return data + Size; }

    void advance(gsl::index cnt)
    {
        Pos += cnt;
        XR_ASSERT(Pos >= 0 && Pos <= Size, "", Pos, Size);
    }

    void close();

    // поиск XR Chunk'ов - возврат - размер или 0
    [[nodiscard]] IReader* open_chunk(u32 ID);

    // iterators
    [[nodiscard]] IReader* open_chunk_iterator(u32& ID, IReader* previous = nullptr); // nullptr => first

    void skip_bom(const char* dbg_name);

public:
    void r(void* p, gsl::index cnt);

    void r_string(char* dest, gsl::index tgt_sz);
    void r_string(xr_string& dest);

    void skip_stringZ();

    void r_stringZ(char* dest, gsl::index tgt_sz);
    void r_stringZ(shared_str& dest);
    void r_stringZ(xr_string& dest);

    [[nodiscard]] Fvector r_vec3() override
    {
        auto tmp = *reinterpret_cast<const Fvector*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] Fvector4 r_vec4() override
    {
        const auto tmp = DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&data[Pos]));
        advance(sizeof(tmp));
        return *reinterpret_cast<const Fvector4*>(&tmp);
    }

    [[nodiscard]] u64 r_u64() override
    {
        auto tmp = *reinterpret_cast<const u64*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] u32 r_u32() override
    {
        auto tmp = *reinterpret_cast<const u32*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] u16 r_u16() override
    {
        auto tmp = *reinterpret_cast<const u16*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] u8 r_u8() override
    {
        auto tmp = *reinterpret_cast<const u8*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] s64 r_s64() override
    {
        auto tmp = *reinterpret_cast<const s64*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] s32 r_s32() override
    {
        auto tmp = *reinterpret_cast<const s32*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] s16 r_s16() override
    {
        auto tmp = *reinterpret_cast<const s16*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] s8 r_s8() override
    {
        auto tmp = *reinterpret_cast<const s8*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] float r_float() override
    {
        auto tmp = *reinterpret_cast<const f32*>(&data[Pos]);
        advance(sizeof(tmp));
        return tmp;
    }

    void r_fvector4(Fvector4& v) override
    {
        *reinterpret_cast<DirectX::XMVECTOR*>(&v) = DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&data[Pos]));
        advance(sizeof(v));
    }

    void r_fvector3(Fvector3& v) override
    {
        v = *reinterpret_cast<const Fvector3*>(&data[Pos]);
        advance(sizeof(v));
    }

    void r_fvector2(Fvector2& v) override
    {
        v = *reinterpret_cast<const Fvector2*>(&data[Pos]);
        advance(sizeof(v));
    }

    void r_ivector4(Ivector4& v) override
    {
        *reinterpret_cast<DirectX::XMVECTOR*>(&v) = DirectX::XMLoadInt4(reinterpret_cast<const u32*>(&data[Pos]));
        advance(sizeof(v));
    }

    void r_ivector3(Ivector3& v) override
    {
        v = *reinterpret_cast<const Ivector3*>(&data[Pos]);
        advance(sizeof(v));
    }

    void r_ivector2(Ivector2& v) override
    {
        v = *reinterpret_cast<const Ivector2*>(&data[Pos]);
        advance(sizeof(v));
    }

    void r_fcolor(Fcolor& v) override
    {
        v = *reinterpret_cast<const Fcolor*>(&data[Pos]);
        advance(sizeof(v));
    }

    [[nodiscard]] float r_float_q16(float min, float max) override
    {
        auto& val = *reinterpret_cast<const u16*>(&data[Pos]);
        advance(sizeof(val));

        float A = (float(val) * (max - min)) / 65535.f + min; // floating-point-error possible
        XR_ASSERT(A >= min - EPS_S && A <= max + EPS_S, "", A, min, max);

        return A;
    }

    [[nodiscard]] float r_float_q8(float min, float max) override
    {
        auto& val = *reinterpret_cast<const u8*>(&data[Pos]);
        advance(sizeof(val));

        float A = (float(val) / 255.0001f) * (max - min) + min; // floating-point-error possible
        XR_ASSERT(A >= min && A <= max, "", A, min, max);

        return A;
    }

    void r_dir(Fvector& A) override
    {
        auto& t = *reinterpret_cast<const u16*>(&data[Pos]);
        advance(sizeof(t));
        pvDecompress(A, t);
    }

    void r_sdir(Fvector& A) override
    {
        auto& t = *reinterpret_cast<const u16*>(&data[Pos]);
        advance(sizeof(t));
        auto& s = *reinterpret_cast<const f32*>(&data[Pos]);
        advance(sizeof(s));
        pvDecompress(A, t);
        A.mul(s);
    }
};

template <>
struct std::default_delete<IReader> final
{
    constexpr void operator()(IReader* ptr) const noexcept { ptr->close(); }
};
