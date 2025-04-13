//
// DynLibUtils
// Copyright (C) 2023 komashchenko (Phoenix)
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#ifndef DYNLIBUTILS_MODULE_HPP
#define DYNLIBUTILS_MODULE_HPP

#pragma once

#include "memaddr.hpp"

#include <emmintrin.h>

#include <array>
#include <cassert>
#include <vector>
#include <string>
#include <string_view>
#include <utility>

#ifdef __cpp_concepts
#	include <concepts>
#endif

#ifdef __cpp_consteval
#	define DYNLIB_COMPILE_TIME_EXPR consteval
#else
#	define DYNLIB_COMPILE_TIME_EXPR constexpr
#endif

namespace DynLibUtils {

struct Section_t
{
	// Constructors.
	Section_t(size_t nSectionSize = 0, const std::string_view& svSectionName = {}, CMemory pSectionBase = nullptr) noexcept : m_nSectionSize(nSectionSize), m_svSectionName(svSectionName), m_pBase(pSectionBase) {} // Default one.
	Section_t(Section_t&& other) noexcept : m_nSectionSize(std::move(other.m_nSectionSize)), m_svSectionName(std::move(other.m_svSectionName)), m_pBase(std::move(other.m_pBase)) {}

	[[nodiscard]]
	bool IsValid() const noexcept { return m_pBase.IsValid(); }

	std::size_t m_nSectionSize;     // Size of the section.
	std::string m_svSectionName;    // Name of the section.
	CMemory m_pBase;                // Start address of the section.
}; // struct Section_t

static constexpr std::size_t s_nDefaultPatternSize = 64;
static constexpr std::size_t s_nMaxSimdBlocks = 1 << 6; // 64 blocks = 1024 bytes per chunk.

template<std::size_t SIZE = 0l>
struct Pattern_t
{
	static constexpr std::size_t sm_nMaxSize = SIZE;

	// Constructors.
	constexpr Pattern_t(const Pattern_t<SIZE>& copyFrom) noexcept : m_nSize(copyFrom.m_nSize), m_aBytes(copyFrom.m_aBytes), m_aMask(copyFrom.m_aMask) {}
	constexpr Pattern_t(Pattern_t<SIZE>&& moveFrom) noexcept : m_nSize(std::move(moveFrom.m_nSize)), m_aBytes(std::move(moveFrom.m_aBytes)), m_aMask(std::move(moveFrom.m_aMask)) {}
	constexpr Pattern_t(std::size_t size = 0, const std::array<uint8_t, SIZE>& bytes = {}, const std::array<char, SIZE>& mask = {}) noexcept : m_nSize(size), m_aBytes(bytes), m_aMask(mask) {} // Default one.
	constexpr Pattern_t(std::size_t &&size, std::array<uint8_t, SIZE>&& bytes, const std::array<char, SIZE>&& mask) noexcept : m_nSize(std::move(size)), m_aBytes(std::move(bytes)), m_aMask(std::move(mask)) {}

	// Fields. Available to anyone (so structure).
	std::size_t m_nSize;
	std::array<std::uint8_t, SIZE> m_aBytes;
	std::array<char, SIZE> m_aMask;
}; // struct Pattern_t

// Concept for pattern callback.
// Signature: bool callback(std::size_t index, CMemory match)
// Returns:   false -> continue scanning.
//            true  -> stop scanning.
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
template<typename T>
concept PatternCallback_t = requires(T func, std::size_t index, CMemory match)
{
	{ func(index, match) } -> std::same_as<bool>;
};
#else
#	define PatternCallback_t typename
#endif

template<std::size_t INDEX = 0, std::size_t N, std::size_t SIZE = (N - 1) / 2>
[[always_inline, nodiscard]]
inline DYNLIB_COMPILE_TIME_EXPR void ProcessStringPattern(const char (&szInput)[N], std::size_t& n, std::size_t& nIndex, std::array<std::uint8_t, SIZE>& aBytes, std::array<char, SIZE>& aMask)
{
	static_assert(SIZE > 0, "Process pattern cannot be empty");

	constexpr auto funcIsHexDigit = [](char c) -> bool
	{
		return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
	};

	constexpr auto funcHexCharToByte = [](char c) -> std::uint8_t
	{
		return ('0' <= c && c <= '9') ? c - '0' : ('A' <= c && c <= 'F') ? c - 'A' + 10 : c - 'a' + 10;
	};

	constexpr std::size_t nLength = N - 1; // Exclude null-terminated character.

	if constexpr (INDEX < nLength)
	{
		const char c = szInput[n];

		if (c == ' ') 
		{
			n++;
			ProcessStringPattern<INDEX + 1>(szInput, n, nIndex, aBytes, aMask);
		}
		else if (c == '?')
		{
			aBytes[nIndex] = 0;
			aMask[nIndex] = '?';

			n++;

			if (n < nLength && szInput[n] == '?')
				n++;

			nIndex++;
			ProcessStringPattern<INDEX + 1>(szInput, n, nIndex, aBytes, aMask);
		}
		else if (funcIsHexDigit(c))
		{
			if (n + 1 < nLength)
			{
				if (funcIsHexDigit(szInput[n + 1]))
				{
					aBytes[nIndex] = (funcHexCharToByte(c) << 4) | funcHexCharToByte(szInput[n + 1]);
					aMask[nIndex] = 'x';

					n += 2;
					nIndex++;
					ProcessStringPattern<INDEX + 1>(szInput, n, nIndex, aBytes, aMask);
				}
				else
				{
					static_assert(R"(Invalid character in pattern. Allowed: <space> or pair: "0-9", "a-f", "A-F" or "?")");
				}
			}
			else
			{
				static_assert("Missing second hexadecimal digit in pattern");
			}
		}
		else
		{
			static_assert("Invalid character in pattern");
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Converts a string pattern with wildcards to an array of bytes and mask
// Input  : svInput - pattern string like "48 8B ?? 89 ?? ?? 41"
// Output : Pattern_t<SIZE> (fixed-size array by N cells with mask and used size)
//----------------------------------------------------------------------------
template<std::size_t N, std::size_t SIZE = (N - 1) / 2>
[[always_inline, nodiscard]]
inline DYNLIB_COMPILE_TIME_EXPR auto ParseStringPattern(const char (&szInput)[N])
{
	static_assert(SIZE > 0, "Pattern cannot be empty");

	std::size_t n = 0;

	Pattern_t<SIZE> result{};

	ProcessStringPattern<0, N, SIZE>(szInput, n, result.m_nSize, result.m_aBytes, result.m_aMask);

	return result;
}

template<std::size_t N = s_nDefaultPatternSize, std::size_t SIZE = (N - 1) / 2>
[[nodiscard]]
inline auto ParsePattern(const std::string_view svInput)
{
	Pattern_t<SIZE> result {};

	auto funcGetHexByte = [](char c) -> uint8_t
	{
		if ('0' <= c && c <= '9') return c - '0';
		if ('a' <= c && c <= 'f') return 10 + (c - 'a');
		if ('A' <= c && c <= 'F') return 10 + (c - 'A');

		return 0;
	};

	size_t n = 0;
	std::uint32_t nOut = 0;

	while (n < svInput.length() && nOut < N)
	{
		if (svInput[n] == '?')
		{
			++n;

			if (n < svInput.size() && svInput[n] == '?')
				++n;

			result.m_aBytes[nOut] = 0x00;
			result.m_aMask[nOut] = '?';
			++nOut;
		}
		else if (n + 1 < svInput.size())
		{
			auto nLeft = funcGetHexByte(svInput[n]), nRight = funcGetHexByte(svInput[n + 1]);

			bool bIsValid = nLeft && nRight;

			assert(bIsValid && R"(Passing invalid characters. Allowed: <space> or pair: "0-9", "a-f", "A-F" or "?")");
			if (!bIsValid)
			{
				++n;
				continue;
			}

			result.m_aBytes[nOut] = (nLeft << 4) | nRight;
			result.m_aMask[nOut] = 'x';
			++nOut;

			n += 2;
		}

		++n;
	}

	result.m_aMask[nOut] = '\0'; // Stores null-terminated character to FindPattern (raw). Don't do (N - 1).
	result.m_nSize = nOut;

	return result;
}

class CModule
{
private:
	std::string m_sPath;
	std::string m_sLastError;
	std::vector<Section_t> m_vecSections;
	const Section_t *m_pExecutableSection;
	void* m_pHandle;

public:
	template<std::size_t SIZE>
	class CSignatureView : public Pattern_t<SIZE>
	{
		using Base_t = Pattern_t<SIZE>;

	private:
		CModule* m_pModule;

	public:
		constexpr CSignatureView() : m_pModule(nullptr) {}
		constexpr CSignatureView(CSignatureView&& moveFrom) : Base_t(std::move(moveFrom)), m_pModule(std::move(moveFrom.m_pModule)) {}
		constexpr CSignatureView(const Base_t& pattern, CModule* module) : Base_t(pattern), m_pModule(module) {}
		constexpr CSignatureView(Base_t&& pattern, CModule* module) : Base_t(std::move(pattern)), m_pModule(module) {}

		[[nodiscard]]
		CMemory operator()(const CMemory pStart = nullptr, const Section_t* pSection = nullptr) const
		{
			return Find(pStart, pSection);
		}

		[[nodiscard]] CMemory Find(const CMemory pStart, const Section_t* pSection) const
		{
			return m_pModule->FindPattern<SIZE>(CMemory(Base_t::m_aBytes.data()), std::string_view(Base_t::m_aMask.data(), Base_t::m_nSize), pStart, pSection);
		}
		[[nodiscard]] CMemory FindAndOffset(const std::ptrdiff_t offset, const CMemory pStart = nullptr, const Section_t* pSection = nullptr) const { return Find(pStart, pSection).Offset(offset); }
		[[nodiscard]] CMemory FindAndOffsetFromSelf(const CMemory pStart = nullptr, const Section_t* pSection = nullptr) const { return FindAndOffset(Base_t::m_nSize, pStart, pSection); }

		[[nodiscard]] CMemory FindAndDeref(const std::uintptr_t deref = 1, const CMemory pStart = nullptr, const Section_t* pSection = nullptr) const { return Find(pStart, pSection).Deref(deref); }
		[[nodiscard]]
		CMemory FollowCall(const std::ptrdiff_t opcodeOffset = 0x1, const std::ptrdiff_t nextInstructionOffset = 0x5, const CMemory pStart = nullptr, const Section_t* pSection = nullptr) const { return Find(pStart, pSection).FollowNearCall(opcodeOffset, nextInstructionOffset); }
	}; // struct CSignatureView

	CModule() : m_pExecutableSection(nullptr), m_pHandle(nullptr) {}
	~CModule();

	CModule(const CModule&) = delete;
	CModule& operator=(const CModule&) = delete;
	CModule(CModule&& other) noexcept : m_sPath(std::move(other.m_sPath)), m_vecSections(std::move(other.m_vecSections)), m_pExecutableSection(std::move(other.m_pExecutableSection)), m_pHandle(std::move(other.m_pHandle)) {}
	explicit CModule(const std::string_view svModuleName);
	explicit CModule(const char* pszModuleName) : CModule(std::string_view(pszModuleName)) {}
	explicit CModule(const std::string& sModuleName) : CModule(std::string_view(sModuleName)) {}
	CModule(const CMemory pModuleMemory);

	bool LoadFromPath(const std::string_view svModelePath, int flags);

	bool InitFromName(const std::string_view svModuleName, bool bExtension = false);
	bool InitFromMemory(const CMemory pModuleMemory, bool bForce = true);

	template<std::size_t N>
	[[always_inline, nodiscard]]
	inline auto CreateSignature(const Pattern_t<N> &copyFrom)
	{
		static_assert(N > 0, "Pattern size must be > 0");

		return CSignatureView<N>(copyFrom, this);
	}

	template<std::size_t N>
	[[always_inline, nodiscard]]
	inline auto CreateSignature(Pattern_t<N> &&moveFrom)
	{
		static_assert(N > 0, "Pattern size must be > 0");

		return CSignatureView<N>(std::move(moveFrom), this);
	}

	//-----------------------------------------------------------------------------
	// Purpose: Finds an array of bytes in process memory using SIMD instructions
	// Input  : *pPattern
	//          svMask
	//          pStartAddress
	//          *pModuleSection
	// Output : CMemory
	//-----------------------------------------------------------------------------
	template<std::size_t SIZE = (s_nDefaultPatternSize - 1) / 2>
	[[always_inline, flatten, hot]]
	inline CMemory FindPattern(const CMemory pPatternMem, const std::string_view svMask, const CMemory pStartAddress, const Section_t* pModuleSection) const
	{
		const auto* pPattern = pPatternMem.RCast<const std::uint8_t*>();

		const Section_t* pSection = pModuleSection ? pModuleSection : m_pExecutableSection;

		if (!pSection || !pSection->IsValid())
			return DYNLIB_INVALID_MEMORY;

		const std::uintptr_t base = pSection->m_pBase;
		const std::size_t sectionSize = pSection->m_nSectionSize;
		const std::size_t patternSize = svMask.size();

		auto* pData = reinterpret_cast<std::uint8_t*>(base);
		const auto* pEnd = pData + sectionSize - patternSize;

		if (pStartAddress)
		{
			auto* start = pStartAddress.RCast<std::uint8_t*>();
			if (start < pData || start > pEnd)
				return DYNLIB_INVALID_MEMORY;

			pData = start;
		}

		constexpr auto kSimdBytes = sizeof(__m128i); // 128 bits = 16 bytes.
		constexpr auto kMaxSimdBlocks = std::max<std::size_t>(1u, std::min<std::size_t>(SIZE, s_nMaxSimdBlocks));

		const std::size_t numBlocks = (patternSize + (kSimdBytes - 1)) / kSimdBytes;

		std::uint16_t bitMasks[kMaxSimdBlocks] = {};
		__m128i patternChunks[kMaxSimdBlocks];

		for (std::size_t n = 0; n < numBlocks; ++n)
		{
			const std::size_t offset = n * kSimdBytes;
			patternChunks[n] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pPattern + offset));

			for (std::size_t j = 0; j < kSimdBytes; ++j)
			{
				const std::size_t idx = offset + j;
				if (idx >= patternSize)
					break;

				if (svMask[idx] == 'x')
					bitMasks[n] |= (1u << j);
			}
		}

		// How far ahead (in bytes) to prefetch during scanning.
		// This is calculated based on how many SIMD blocks (16 bytes each) will be read 
		// in the current pattern match attempt.
		//
		// Helps reduce cache misses during large linear memory scans by hinting the CPU 
		// to load the next block of memory before it is needed.
		const std::size_t lookAhead = numBlocks * kSimdBytes;

		for (; pData <= pEnd; ++pData)
		{
			if (static_cast<std::size_t>(pEnd - pData) > lookAhead)
				_mm_prefetch(reinterpret_cast<const char*>(pData + lookAhead), _MM_HINT_NTA);

			bool bFound = true;

			for (std::size_t n = 0; n < numBlocks; ++n)
			{
				const __m128i dataChunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pData + n * kSimdBytes));
				const __m128i cmp = _mm_cmpeq_epi8(dataChunk, patternChunks[n]);
				const int mask = _mm_movemask_epi8(cmp);

				if ((mask & bitMasks[n]) != bitMasks[n])
				{
					bFound = false;
					break;
				}
			}

			if (bFound)
				return pData;
		}

		return DYNLIB_INVALID_MEMORY;
	}

	template<std::size_t SIZE>
	[[nodiscard]]
	inline CMemory FindPattern(const Pattern_t<SIZE>& copyPattern, const CMemory pStartAddress = nullptr, const Section_t* pModuleSection = nullptr) const
	{
		return FindPattern<SIZE>(CMemory(copyPattern.m_aBytes.data()), std::string_view(copyPattern.m_aMask.data(), copyPattern.m_nSize), pStartAddress, pModuleSection);
	}

	template<std::size_t SIZE>
	[[nodiscard]]
	inline CMemory FindPattern(Pattern_t<SIZE>&& movePattern, const CMemory pStartAddress = nullptr, const Section_t* pModuleSection = nullptr) const
	{
		return FindPattern<SIZE>(CMemory(std::move(movePattern.m_aBytes).data()), std::string_view(std::move(movePattern.m_aMask).data(), std::move(movePattern.m_nSize)), pStartAddress, pModuleSection);
	}

	template<std::size_t SIZE, PatternCallback_t FUNC>
	std::size_t FindAllPatterns(const CSignatureView<SIZE>& sig, const FUNC& callback, CMemory pStartAddress = nullptr, const Section_t* pModuleSection = nullptr) const
	{
		const Section_t* pSection = pModuleSection ? pModuleSection : m_pExecutableSection;

		if (!pSection || !pSection->IsValid())
			return 0;

		const CMemory pBase = pSection->m_pBase;
		const std::size_t sectionSize = pSection->m_nSectionSize;

		CMemory pIter = pStartAddress ? pStartAddress : pBase;
		const CMemory pEnd = pBase + sectionSize;

		std::size_t foundLength = 0;

		for (CMemory pMatch = sig(pIter, pSection); 
		     pMatch.IsValid() && 
		     pMatch < pEnd; 
		     pIter = sig.FindAndOffsetFromSelf(pMatch, pSection))
		{
			if (callback(foundLength, pMatch)) // foundLength = the index of found pattern now.
				break;

			++foundLength;
		}

		return foundLength; // Count of the found patterns.
	}

	[[nodiscard]] CMemory GetVirtualTableByName(const std::string_view svTableName, bool bDecorated = false) const;
	[[nodiscard]] CMemory GetFunctionByName(const std::string_view svFunctionName) const noexcept;

	[[nodiscard]] void* GetHandle() const noexcept { return m_pHandle; }
	[[nodiscard]] CMemory GetBase() const noexcept;
	[[nodiscard]] std::string_view GetPath() const { return m_sPath; }
	[[nodiscard]] std::string_view GetLastError() const { return m_sLastError; }
	[[nodiscard]] std::string_view GetName() const { std::string_view svModulePath(m_sPath); return svModulePath.substr(svModulePath.find_last_of("/\\") + 1); }
	[[nodiscard]] const Section_t *GetSectionByName(const std::string_view svSectionName) const
	{
		for (const auto& section : m_vecSections)
			if (svSectionName == section.m_svSectionName)
				return &section;

		return nullptr;
	}

protected:
	void SaveLastError();
}; // class CModule

} // namespace DynLibUtils

#endif // DYNLIBUTILS_MODULE_HPP
