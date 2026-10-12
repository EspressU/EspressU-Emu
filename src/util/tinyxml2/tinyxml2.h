
#ifndef TINYXML2_INCLUDED
#define TINYXML2_INCLUDED

#if defined(ANDROID_NDK) || defined(__BORLANDC__) || defined(__QNXNTO__)
#   include <ctype.h>
#   include <limits.h>
#   include <stdio.h>
#   include <stdlib.h>
#   include <string.h>
#	if defined(__PS3__)
#		include <stddef.h>
#	endif
#else
#   include <cctype>
#   include <climits>
#   include <cstdio>
#   include <cstdlib>
#   include <cstring>
#endif
#include <stdint.h>

#if defined( _DEBUG ) || defined (__DEBUG__)
#   ifndef DEBUG
#       define DEBUG
#   endif
#endif

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4251)
#endif

#ifdef _WIN32
#   ifdef TINYXML2_EXPORT
#       define TINYXML2_LIB __declspec(dllexport)
#   elif defined(TINYXML2_IMPORT)
#       define TINYXML2_LIB __declspec(dllimport)
#   else
#       define TINYXML2_LIB
#   endif
#elif __GNUC__ >= 4
#   define TINYXML2_LIB __attribute__((visibility("default")))
#else
#   define TINYXML2_LIB
#endif

#if defined(DEBUG)
#   if defined(_MSC_VER)
#
#       define TIXMLASSERT( x )           if ( !((void)0,(x))) { __debugbreak(); }
#   elif defined (ANDROID_NDK)
#       include <android/log.h>
#       define TIXMLASSERT( x )           if ( !(x)) { __android_log_assert( "assert", "grinliz", "ASSERT in '%s' at %d.", __FILE__, __LINE__ ); }
#   else
#       include <assert.h>
#       define TIXMLASSERT                assert
#   endif
#else
#   define TIXMLASSERT( x )               {}
#endif

static const int TIXML2_MAJOR_VERSION = 5;
static const int TIXML2_MINOR_VERSION = 0;
static const int TIXML2_PATCH_VERSION = 1;

namespace tinyxml2
{
	class XMLDocument;
	class XMLElement;
	class XMLAttribute;
	class XMLComment;
	class XMLText;
	class XMLDeclaration;
	class XMLUnknown;
	class XMLPrinter;

	class StrPair
	{
	public:
		enum {
			NEEDS_ENTITY_PROCESSING = 0x01,
			NEEDS_NEWLINE_NORMALIZATION = 0x02,
			NEEDS_WHITESPACE_COLLAPSING = 0x04,

			TEXT_ELEMENT = NEEDS_ENTITY_PROCESSING | NEEDS_NEWLINE_NORMALIZATION,
			TEXT_ELEMENT_LEAVE_ENTITIES = NEEDS_NEWLINE_NORMALIZATION,
			ATTRIBUTE_NAME = 0,
			ATTRIBUTE_VALUE = NEEDS_ENTITY_PROCESSING | NEEDS_NEWLINE_NORMALIZATION,
			ATTRIBUTE_VALUE_LEAVE_ENTITIES = NEEDS_NEWLINE_NORMALIZATION,
			COMMENT = NEEDS_NEWLINE_NORMALIZATION
		};

		StrPair() : _flags(0), _start(0), _end(0) {}
		~StrPair();

		void Set(char* start, char* end, int flags) {
			TIXMLASSERT(start);
			TIXMLASSERT(end);
			Reset();
			_start = start;
			_end = end;
			_flags = flags | NEEDS_FLUSH;
		}

		const char* GetStr();

		bool Empty() const {
			return _start == _end;
		}

		void SetInternedStr(const char* str) {
			Reset();
			_start = const_cast<char*>(str);
		}

		void SetStr(const char* str, int flags = 0);

		char* ParseText(char* in, const char* endTag, int strFlags, int* curLineNumPtr);
		char* ParseName(char* in);

		void TransferTo(StrPair* other);
		void Reset();

	private:
		void CollapseWhitespace();

		enum {
			NEEDS_FLUSH = 0x100,
			NEEDS_DELETE = 0x200
		};

		int     _flags;
		char*   _start;
		char*   _end;

		StrPair(const StrPair& other);
		void operator=(StrPair& other);
	};

	template <class T, int INITIAL_SIZE>
	class DynArray
	{
	public:
		DynArray() :
			_mem(_pool),
			_allocated(INITIAL_SIZE),
			_size(0)
		{
		}

		~DynArray() {
			if (_mem != _pool) {
				delete[] _mem;
			}
		}

		void Clear() {
			_size = 0;
		}

		void Push(T t) {
			TIXMLASSERT(_size < INT_MAX);
			EnsureCapacity(_size + 1);
			_mem[_size] = t;
			++_size;
		}

		T* PushArr(int count) {
			TIXMLASSERT(count >= 0);
			TIXMLASSERT(_size <= INT_MAX - count);
			EnsureCapacity(_size + count);
			T* ret = &_mem[_size];
			_size += count;
			return ret;
		}

		T Pop() {
			TIXMLASSERT(_size > 0);
			--_size;
			return _mem[_size];
		}

		void PopArr(int count) {
			TIXMLASSERT(_size >= count);
			_size -= count;
		}

		bool Empty() const {
			return _size == 0;
		}

		T& operator[](int i) {
			TIXMLASSERT(i >= 0 && i < _size);
			return _mem[i];
		}

		const T& operator[](int i) const {
			TIXMLASSERT(i >= 0 && i < _size);
			return _mem[i];
		}

		const T& PeekTop() const {
			TIXMLASSERT(_size > 0);
			return _mem[_size - 1];
		}

		int Size() const {
			TIXMLASSERT(_size >= 0);
			return _size;
		}

		int Capacity() const {
			TIXMLASSERT(_allocated >= INITIAL_SIZE);
			return _allocated;
		}

		void SwapRemove(int i) {
			TIXMLASSERT(i >= 0 && i < _size);
			TIXMLASSERT(_size > 0);
			_mem[i] = _mem[_size - 1];
			--_size;
		}

		const T* Mem() const {
			TIXMLASSERT(_mem);
			return _mem;
		}

		T* Mem() {
			TIXMLASSERT(_mem);
			return _mem;
		}

	private:
		DynArray(const DynArray&);
		void operator=(const DynArray&);

		void EnsureCapacity(int cap) {
			TIXMLASSERT(cap > 0);
			if (cap > _allocated) {
				TIXMLASSERT(cap <= INT_MAX / 2);
				int newAllocated = cap * 2;
				T* newMem = new T[newAllocated];
				TIXMLASSERT(newAllocated >= _size);
				memcpy(newMem, _mem, sizeof(T)*_size);
				if (_mem != _pool) {
					delete[] _mem;
				}
				_mem = newMem;
				_allocated = newAllocated;
			}
		}

		T*  _mem;
		T   _pool[INITIAL_SIZE];
		int _allocated;
		int _size;
	};

	class MemPool
	{
	public:
		MemPool() {}
		virtual ~MemPool() {}

		virtual int ItemSize() const = 0;
		virtual void* Alloc() = 0;
		virtual void Free(void*) = 0;
		virtual void SetTracked() = 0;
		virtual void Clear() = 0;
	};

	template< int ITEM_SIZE >
	class MemPoolT : public MemPool
	{
	public:
		MemPoolT() : _blockPtrs(), _root(0), _currentAllocs(0), _nAllocs(0), _maxAllocs(0), _nUntracked(0) {}
		~MemPoolT() {
			Clear();
		}

		void Clear() {
			while (!_blockPtrs.Empty()) {
				Block* lastBlock = _blockPtrs.Pop();
				delete lastBlock;
			}
			_root = 0;
			_currentAllocs = 0;
			_nAllocs = 0;
			_maxAllocs = 0;
			_nUntracked = 0;
		}

		virtual int ItemSize() const {
			return ITEM_SIZE;
		}
		int CurrentAllocs() const {
			return _currentAllocs;
		}

		virtual void* Alloc() {
			if (!_root) {
				Block* block = new Block();
				_blockPtrs.Push(block);

				Item* blockItems = block->items;
				for (int i = 0; i < ITEMS_PER_BLOCK - 1; ++i) {
					blockItems[i].next = &(blockItems[i + 1]);
				}
				blockItems[ITEMS_PER_BLOCK - 1].next = 0;
				_root = blockItems;
			}
			Item* const result = _root;
			TIXMLASSERT(result != 0);
			_root = _root->next;

			++_currentAllocs;
			if (_currentAllocs > _maxAllocs) {
				_maxAllocs = _currentAllocs;
			}
			++_nAllocs;
			++_nUntracked;
			return result;
		}

		virtual void Free(void* mem) {
			if (!mem) {
				return;
			}
			--_currentAllocs;
			Item* item = static_cast<Item*>(mem);
#ifdef DEBUG
			memset(item, 0xfe, sizeof(*item));
#endif
			item->next = _root;
			_root = item;
		}
		void Trace(const char* name) {
			printf("Mempool %s watermark=%d [%dk] current=%d size=%d nAlloc=%d blocks=%d\n",
				name, _maxAllocs, _maxAllocs * ITEM_SIZE / 1024, _currentAllocs,
				ITEM_SIZE, _nAllocs, _blockPtrs.Size());
		}

		void SetTracked() {
			--_nUntracked;
		}

		int Untracked() const {
			return _nUntracked;
		}

		enum { ITEMS_PER_BLOCK = (4 * 1024) / ITEM_SIZE };

	private:
		MemPoolT(const MemPoolT&);
		void operator=(const MemPoolT&);

		union Item {
			Item*   next;
			char    itemData[ITEM_SIZE];
		};
		struct Block {
			Item items[ITEMS_PER_BLOCK];
		};
		DynArray< Block*, 10 > _blockPtrs;
		Item* _root;

		int _currentAllocs;
		int _nAllocs;
		int _maxAllocs;
		int _nUntracked;
	};

	class TINYXML2_LIB XMLVisitor
	{
	public:
		virtual ~XMLVisitor() {}

		virtual bool VisitEnter(const XMLDocument&  ) {
			return true;
		}
		virtual bool VisitExit(const XMLDocument&  ) {
			return true;
		}

		virtual bool VisitEnter(const XMLElement&  , const XMLAttribute*  ) {
			return true;
		}
		virtual bool VisitExit(const XMLElement&  ) {
			return true;
		}

		virtual bool Visit(const XMLDeclaration&  ) {
			return true;
		}
		virtual bool Visit(const XMLText&  ) {
			return true;
		}
		virtual bool Visit(const XMLComment&  ) {
			return true;
		}
		virtual bool Visit(const XMLUnknown&  ) {
			return true;
		}
	};

	enum XMLError {
		XML_SUCCESS = 0,
		XML_NO_ATTRIBUTE,
		XML_WRONG_ATTRIBUTE_TYPE,
		XML_ERROR_FILE_NOT_FOUND,
		XML_ERROR_FILE_COULD_NOT_BE_OPENED,
		XML_ERROR_FILE_READ_ERROR,
		UNUSED_XML_ERROR_ELEMENT_MISMATCH,
		XML_ERROR_PARSING_ELEMENT,
		XML_ERROR_PARSING_ATTRIBUTE,
		UNUSED_XML_ERROR_IDENTIFYING_TAG,
		XML_ERROR_PARSING_TEXT,
		XML_ERROR_PARSING_CDATA,
		XML_ERROR_PARSING_COMMENT,
		XML_ERROR_PARSING_DECLARATION,
		XML_ERROR_PARSING_UNKNOWN,
		XML_ERROR_EMPTY_DOCUMENT,
		XML_ERROR_MISMATCHED_ELEMENT,
		XML_ERROR_PARSING,
		XML_CAN_NOT_CONVERT_TEXT,
		XML_NO_TEXT_NODE,

		XML_ERROR_COUNT
	};

	class TINYXML2_LIB XMLUtil
	{
	public:
		static const char* SkipWhiteSpace(const char* p, int* curLineNumPtr) {
			TIXMLASSERT(p);

			while (IsWhiteSpace(*p)) {
				if (curLineNumPtr && *p == '\n') {
					++(*curLineNumPtr);
				}
				++p;
			}
			TIXMLASSERT(p);
			return p;
		}
		static char* SkipWhiteSpace(char* p, int* curLineNumPtr) {
			return const_cast<char*>(SkipWhiteSpace(const_cast<const char*>(p), curLineNumPtr));
		}

		static bool IsWhiteSpace(char p) {
			return !IsUTF8Continuation(p) && isspace(static_cast<unsigned char>(p));
		}

		inline static bool IsNameStartChar(unsigned char ch) {
			if (ch >= 128) {
				return true;
			}
			if (isalpha(ch)) {
				return true;
			}
			return ch == ':' || ch == '_';
		}

		inline static bool IsNameChar(unsigned char ch) {
			return IsNameStartChar(ch)
				|| isdigit(ch)
				|| ch == '.'
				|| ch == '-';
		}

		inline static bool StringEqual(const char* p, const char* q, int nChar = INT_MAX) {
			if (p == q) {
				return true;
			}
			TIXMLASSERT(p);
			TIXMLASSERT(q);
			TIXMLASSERT(nChar >= 0);
			return strncmp(p, q, nChar) == 0;
		}

		inline static bool IsUTF8Continuation(char p) {
			return (p & 0x80) != 0;
		}

		static const char* ReadBOM(const char* p, bool* hasBOM);
		static const char* GetCharacterRef(const char* p, char* value, int* length);
		static void ConvertUTF32ToUTF8(unsigned long input, char* output, int* length);

		static void ToStr(int v, char* buffer, int bufferSize);
		static void ToStr(unsigned v, char* buffer, int bufferSize);
		static void ToStr(bool v, char* buffer, int bufferSize);
		static void ToStr(float v, char* buffer, int bufferSize);
		static void ToStr(double v, char* buffer, int bufferSize);
		static void ToStr(int64_t v, char* buffer, int bufferSize);

		static bool	ToInt(const char* str, int* value);
		static bool ToUnsigned(const char* str, unsigned* value);
		static bool	ToBool(const char* str, bool* value);
		static bool	ToFloat(const char* str, float* value);
		static bool ToDouble(const char* str, double* value);
		static bool ToInt64(const char* str, int64_t* value);

		static void SetBoolSerialization(const char* writeTrue, const char* writeFalse);

	private:
		static const char* writeBoolTrue;
		static const char* writeBoolFalse;
	};

	class TINYXML2_LIB XMLNode
	{
		friend class XMLDocument;
		friend class XMLElement;
	public:

		const XMLDocument* GetDocument() const {
			TIXMLASSERT(_document);
			return _document;
		}
		XMLDocument* GetDocument() {
			TIXMLASSERT(_document);
			return _document;
		}

		virtual XMLElement*		ToElement() {
			return 0;
		}
		virtual XMLText*		ToText() {
			return 0;
		}
		virtual XMLComment*		ToComment() {
			return 0;
		}
		virtual XMLDocument*	ToDocument() {
			return 0;
		}
		virtual XMLDeclaration*	ToDeclaration() {
			return 0;
		}
		virtual XMLUnknown*		ToUnknown() {
			return 0;
		}

		virtual const XMLElement*		ToElement() const {
			return 0;
		}
		virtual const XMLText*			ToText() const {
			return 0;
		}
		virtual const XMLComment*		ToComment() const {
			return 0;
		}
		virtual const XMLDocument*		ToDocument() const {
			return 0;
		}
		virtual const XMLDeclaration*	ToDeclaration() const {
			return 0;
		}
		virtual const XMLUnknown*		ToUnknown() const {
			return 0;
		}

		const char* Value() const;

		void SetValue(const char* val, bool staticMem = false);

		int GetLineNum() const { return _parseLineNum; }

		const XMLNode*	Parent() const {
			return _parent;
		}

		XMLNode* Parent() {
			return _parent;
		}

		bool NoChildren() const {
			return !_firstChild;
		}

		const XMLNode*  FirstChild() const {
			return _firstChild;
		}

		XMLNode*		FirstChild() {
			return _firstChild;
		}

		const XMLElement* FirstChildElement(const char* name = 0) const;

		XMLElement* FirstChildElement(const char* name = 0) {
			return const_cast<XMLElement*>(const_cast<const XMLNode*>(this)->FirstChildElement(name));
		}

		const XMLNode*	LastChild() const {
			return _lastChild;
		}

		XMLNode*		LastChild() {
			return _lastChild;
		}

		const XMLElement* LastChildElement(const char* name = 0) const;

		XMLElement* LastChildElement(const char* name = 0) {
			return const_cast<XMLElement*>(const_cast<const XMLNode*>(this)->LastChildElement(name));
		}

		const XMLNode*	PreviousSibling() const {
			return _prev;
		}

		XMLNode*	PreviousSibling() {
			return _prev;
		}

		const XMLElement*	PreviousSiblingElement(const char* name = 0) const;

		XMLElement*	PreviousSiblingElement(const char* name = 0) {
			return const_cast<XMLElement*>(const_cast<const XMLNode*>(this)->PreviousSiblingElement(name));
		}

		const XMLNode*	NextSibling() const {
			return _next;
		}

		XMLNode*	NextSibling() {
			return _next;
		}

		const XMLElement*	NextSiblingElement(const char* name = 0) const;

		XMLElement*	NextSiblingElement(const char* name = 0) {
			return const_cast<XMLElement*>(const_cast<const XMLNode*>(this)->NextSiblingElement(name));
		}

		XMLNode* InsertEndChild(XMLNode* addThis);

		XMLNode* LinkEndChild(XMLNode* addThis) {
			return InsertEndChild(addThis);
		}
		XMLNode* InsertFirstChild(XMLNode* addThis);
		XMLNode* InsertAfterChild(XMLNode* afterThis, XMLNode* addThis);

		void DeleteChildren();

		void DeleteChild(XMLNode* node);

		virtual XMLNode* ShallowClone(XMLDocument* document) const = 0;

		XMLNode* DeepClone(XMLDocument* target) const;

		virtual bool ShallowEqual(const XMLNode* compare) const = 0;

		virtual bool Accept(XMLVisitor* visitor) const = 0;

		void SetUserData(void* userData) { _userData = userData; }

		void* GetUserData() const { return _userData; }

	protected:
		XMLNode(XMLDocument*);
		virtual ~XMLNode();

		virtual char* ParseDeep(char* p, StrPair* parentEndTag, int* curLineNumPtr);

		XMLDocument*	_document;
		XMLNode*		_parent;
		mutable StrPair	_value;
		int             _parseLineNum;

		XMLNode*		_firstChild;
		XMLNode*		_lastChild;

		XMLNode*		_prev;
		XMLNode*		_next;

		void*			_userData;

	private:
		MemPool*		_memPool;
		void Unlink(XMLNode* child);
		static void DeleteNode(XMLNode* node);
		void InsertChildPreamble(XMLNode* insertThis) const;
		const XMLElement* ToElementWithName(const char* name) const;

		XMLNode(const XMLNode&);
		XMLNode& operator=(const XMLNode&);
	};

	class TINYXML2_LIB XMLText : public XMLNode
	{
		friend class XMLDocument;
	public:
		virtual bool Accept(XMLVisitor* visitor) const;

		virtual XMLText* ToText() {
			return this;
		}
		virtual const XMLText* ToText() const {
			return this;
		}

		void SetCData(bool isCData) {
			_isCData = isCData;
		}
		bool CData() const {
			return _isCData;
		}

		virtual XMLNode* ShallowClone(XMLDocument* document) const;
		virtual bool ShallowEqual(const XMLNode* compare) const;

	protected:
		XMLText(XMLDocument* doc) : XMLNode(doc), _isCData(false) {}
		virtual ~XMLText() {}

		char* ParseDeep(char* p, StrPair* parentEndTag, int* curLineNumPtr);

	private:
		bool _isCData;

		XMLText(const XMLText&);
		XMLText& operator=(const XMLText&);
	};

	class TINYXML2_LIB XMLComment : public XMLNode
	{
		friend class XMLDocument;
	public:
		virtual XMLComment*	ToComment() {
			return this;
		}
		virtual const XMLComment* ToComment() const {
			return this;
		}

		virtual bool Accept(XMLVisitor* visitor) const;

		virtual XMLNode* ShallowClone(XMLDocument* document) const;
		virtual bool ShallowEqual(const XMLNode* compare) const;

	protected:
		XMLComment(XMLDocument* doc);
		virtual ~XMLComment();

		char* ParseDeep(char* p, StrPair* parentEndTag, int* curLineNumPtr);

	private:
		XMLComment(const XMLComment&);
		XMLComment& operator=(const XMLComment&);
	};

	class TINYXML2_LIB XMLDeclaration : public XMLNode
	{
		friend class XMLDocument;
	public:
		virtual XMLDeclaration*	ToDeclaration() {
			return this;
		}
		virtual const XMLDeclaration* ToDeclaration() const {
			return this;
		}

		virtual bool Accept(XMLVisitor* visitor) const;

		virtual XMLNode* ShallowClone(XMLDocument* document) const;
		virtual bool ShallowEqual(const XMLNode* compare) const;

	protected:
		XMLDeclaration(XMLDocument* doc);
		virtual ~XMLDeclaration();

		char* ParseDeep(char* p, StrPair* parentEndTag, int* curLineNumPtr);

	private:
		XMLDeclaration(const XMLDeclaration&);
		XMLDeclaration& operator=(const XMLDeclaration&);
	};

	class TINYXML2_LIB XMLUnknown : public XMLNode
	{
		friend class XMLDocument;
	public:
		virtual XMLUnknown*	ToUnknown() {
			return this;
		}
		virtual const XMLUnknown* ToUnknown() const {
			return this;
		}

		virtual bool Accept(XMLVisitor* visitor) const;

		virtual XMLNode* ShallowClone(XMLDocument* document) const;
		virtual bool ShallowEqual(const XMLNode* compare) const;

	protected:
		XMLUnknown(XMLDocument* doc);
		virtual ~XMLUnknown();

		char* ParseDeep(char* p, StrPair* parentEndTag, int* curLineNumPtr);

	private:
		XMLUnknown(const XMLUnknown&);
		XMLUnknown& operator=(const XMLUnknown&);
	};

	class TINYXML2_LIB XMLAttribute
	{
		friend class XMLElement;
	public:
		const char* Name() const;

		const char* Value() const;

		int GetLineNum() const { return _parseLineNum; }

		const XMLAttribute* Next() const {
			return _next;
		}

		int	IntValue() const {
			int i = 0;
			QueryIntValue(&i);
			return i;
		}

		int64_t Int64Value() const {
			int64_t i = 0;
			QueryInt64Value(&i);
			return i;
		}

		unsigned UnsignedValue() const {
			unsigned i = 0;
			QueryUnsignedValue(&i);
			return i;
		}
		bool	 BoolValue() const {
			bool b = false;
			QueryBoolValue(&b);
			return b;
		}
		double 	 DoubleValue() const {
			double d = 0;
			QueryDoubleValue(&d);
			return d;
		}
		float	 FloatValue() const {
			float f = 0;
			QueryFloatValue(&f);
			return f;
		}

		XMLError QueryIntValue(int* value) const;
		XMLError QueryUnsignedValue(unsigned int* value) const;
		XMLError QueryInt64Value(int64_t* value) const;
		XMLError QueryBoolValue(bool* value) const;
		XMLError QueryDoubleValue(double* value) const;
		XMLError QueryFloatValue(float* value) const;

		void SetAttribute(const char* value);
		void SetAttribute(int value);
		void SetAttribute(unsigned value);
		void SetAttribute(int64_t value);
		void SetAttribute(bool value);
		void SetAttribute(double value);
		void SetAttribute(float value);

	private:
		enum { BUF_SIZE = 200 };

		XMLAttribute() : _name(), _value(), _parseLineNum(0), _next(0), _memPool(0) {}
		virtual ~XMLAttribute() {}

		XMLAttribute(const XMLAttribute&);
		void operator=(const XMLAttribute&);
		void SetName(const char* name);

		char* ParseDeep(char* p, bool processEntities, int* curLineNumPtr);

		mutable StrPair _name;
		mutable StrPair _value;
		int             _parseLineNum;
		XMLAttribute*   _next;
		MemPool*        _memPool;
	};

	class TINYXML2_LIB XMLElement : public XMLNode
	{
		friend class XMLDocument;
	public:
		const char* Name() const {
			return Value();
		}
		void SetName(const char* str, bool staticMem = false) {
			SetValue(str, staticMem);
		}

		virtual XMLElement* ToElement() {
			return this;
		}
		virtual const XMLElement* ToElement() const {
			return this;
		}
		virtual bool Accept(XMLVisitor* visitor) const;

		const char* Attribute(const char* name, const char* value = 0) const;

		int IntAttribute(const char* name, int defaultValue = 0) const;
		unsigned UnsignedAttribute(const char* name, unsigned defaultValue = 0) const;
		int64_t Int64Attribute(const char* name, int64_t defaultValue = 0) const;
		bool BoolAttribute(const char* name, bool defaultValue = false) const;
		double DoubleAttribute(const char* name, double defaultValue = 0) const;
		float FloatAttribute(const char* name, float defaultValue = 0) const;

		XMLError QueryIntAttribute(const char* name, int* value) const {
			const XMLAttribute* a = FindAttribute(name);
			if (!a) {
				return XML_NO_ATTRIBUTE;
			}
			return a->QueryIntValue(value);
		}

		XMLError QueryUnsignedAttribute(const char* name, unsigned int* value) const {
			const XMLAttribute* a = FindAttribute(name);
			if (!a) {
				return XML_NO_ATTRIBUTE;
			}
			return a->QueryUnsignedValue(value);
		}

		XMLError QueryInt64Attribute(const char* name, int64_t* value) const {
			const XMLAttribute* a = FindAttribute(name);
			if (!a) {
				return XML_NO_ATTRIBUTE;
			}
			return a->QueryInt64Value(value);
		}

		XMLError QueryBoolAttribute(const char* name, bool* value) const {
			const XMLAttribute* a = FindAttribute(name);
			if (!a) {
				return XML_NO_ATTRIBUTE;
			}
			return a->QueryBoolValue(value);
		}
		XMLError QueryDoubleAttribute(const char* name, double* value) const {
			const XMLAttribute* a = FindAttribute(name);
			if (!a) {
				return XML_NO_ATTRIBUTE;
			}
			return a->QueryDoubleValue(value);
		}
		XMLError QueryFloatAttribute(const char* name, float* value) const {
			const XMLAttribute* a = FindAttribute(name);
			if (!a) {
				return XML_NO_ATTRIBUTE;
			}
			return a->QueryFloatValue(value);
		}

		int QueryAttribute(const char* name, int* value) const {
			return QueryIntAttribute(name, value);
		}

		int QueryAttribute(const char* name, unsigned int* value) const {
			return QueryUnsignedAttribute(name, value);
		}

		int QueryAttribute(const char* name, int64_t* value) const {
			return QueryInt64Attribute(name, value);
		}

		int QueryAttribute(const char* name, bool* value) const {
			return QueryBoolAttribute(name, value);
		}

		int QueryAttribute(const char* name, double* value) const {
			return QueryDoubleAttribute(name, value);
		}

		int QueryAttribute(const char* name, float* value) const {
			return QueryFloatAttribute(name, value);
		}

		void SetAttribute(const char* name, const char* value) {
			XMLAttribute* a = FindOrCreateAttribute(name);
			a->SetAttribute(value);
		}
		void SetAttribute(const char* name, int value) {
			XMLAttribute* a = FindOrCreateAttribute(name);
			a->SetAttribute(value);
		}
		void SetAttribute(const char* name, unsigned value) {
			XMLAttribute* a = FindOrCreateAttribute(name);
			a->SetAttribute(value);
		}

		void SetAttribute(const char* name, int64_t value) {
			XMLAttribute* a = FindOrCreateAttribute(name);
			a->SetAttribute(value);
		}

		void SetAttribute(const char* name, bool value) {
			XMLAttribute* a = FindOrCreateAttribute(name);
			a->SetAttribute(value);
		}
		void SetAttribute(const char* name, double value) {
			XMLAttribute* a = FindOrCreateAttribute(name);
			a->SetAttribute(value);
		}
		void SetAttribute(const char* name, float value) {
			XMLAttribute* a = FindOrCreateAttribute(name);
			a->SetAttribute(value);
		}

		void DeleteAttribute(const char* name);

		const XMLAttribute* FirstAttribute() const {
			return _rootAttribute;
		}
		const XMLAttribute* FindAttribute(const char* name) const;

		const char* GetText() const;

		void SetText(const char* inText);
		void SetText(int value);
		void SetText(unsigned value);
		void SetText(int64_t value);
		void SetText(bool value);
		void SetText(double value);
		void SetText(float value);

		XMLError QueryIntText(int* ival) const;
		XMLError QueryUnsignedText(unsigned* uval) const;
		XMLError QueryInt64Text(int64_t* uval) const;
		XMLError QueryBoolText(bool* bval) const;
		XMLError QueryDoubleText(double* dval) const;
		XMLError QueryFloatText(float* fval) const;

		int IntText(int defaultValue = 0) const;

		unsigned UnsignedText(unsigned defaultValue = 0) const;
		int64_t Int64Text(int64_t defaultValue = 0) const;
		bool BoolText(bool defaultValue = false) const;
		double DoubleText(double defaultValue = 0) const;
		float FloatText(float defaultValue = 0) const;

		enum ElementClosingType {
			OPEN,
			CLOSED,
			CLOSING
		};
		ElementClosingType ClosingType() const {
			return _closingType;
		}
		virtual XMLNode* ShallowClone(XMLDocument* document) const;
		virtual bool ShallowEqual(const XMLNode* compare) const;

	protected:
		char* ParseDeep(char* p, StrPair* parentEndTag, int* curLineNumPtr);

	private:
		XMLElement(XMLDocument* doc);
		virtual ~XMLElement();
		XMLElement(const XMLElement&);
		void operator=(const XMLElement&);

		XMLAttribute* FindAttribute(const char* name) {
			return const_cast<XMLAttribute*>(const_cast<const XMLElement*>(this)->FindAttribute(name));
		}
		XMLAttribute* FindOrCreateAttribute(const char* name);
		char* ParseAttributes(char* p, int* curLineNumPtr);
		static void DeleteAttribute(XMLAttribute* attribute);
		XMLAttribute* CreateAttribute();

		enum { BUF_SIZE = 200 };
		ElementClosingType _closingType;
		XMLAttribute* _rootAttribute;
	};

	enum Whitespace {
		PRESERVE_WHITESPACE,
		COLLAPSE_WHITESPACE
	};

	class TINYXML2_LIB XMLDocument : public XMLNode
	{
		friend class XMLElement;
	public:
		XMLDocument(bool processEntities = true, Whitespace whitespaceMode = PRESERVE_WHITESPACE);
		~XMLDocument();

		virtual XMLDocument* ToDocument() {
			TIXMLASSERT(this == _document);
			return this;
		}
		virtual const XMLDocument* ToDocument() const {
			TIXMLASSERT(this == _document);
			return this;
		}

		XMLError Parse(const char* xml, size_t nBytes = (size_t)(-1));

		XMLError LoadFile(const char* filename);

		XMLError LoadFile(FILE*);

		XMLError SaveFile(const char* filename, bool compact = false);

		XMLError SaveFile(FILE* fp, bool compact = false);

		bool ProcessEntities() const {
			return _processEntities;
		}
		Whitespace WhitespaceMode() const {
			return _whitespaceMode;
		}

		bool HasBOM() const {
			return _writeBOM;
		}
		void SetBOM(bool useBOM) {
			_writeBOM = useBOM;
		}

		XMLElement* RootElement() {
			return FirstChildElement();
		}
		const XMLElement* RootElement() const {
			return FirstChildElement();
		}

		void Print(XMLPrinter* streamer = 0) const;
		virtual bool Accept(XMLVisitor* visitor) const;

		XMLElement* NewElement(const char* name);
		XMLComment* NewComment(const char* comment);
		XMLText* NewText(const char* text);
		XMLDeclaration* NewDeclaration(const char* text = 0);
		XMLUnknown* NewUnknown(const char* text);

		void DeleteNode(XMLNode* node);

		void SetError(XMLError error, const char* str1, const char* str2, int lineNum);

		void ClearError() {
			SetError(XML_SUCCESS, 0, 0, 0);
		}

		bool Error() const {
			return _errorID != XML_SUCCESS;
		}
		XMLError  ErrorID() const {
			return _errorID;
		}
		const char* ErrorName() const;
		static const char* ErrorIDToName(XMLError errorID);

		const char* GetErrorStr1() const;

		const char* GetErrorStr2() const;

		int GetErrorLineNum() const
		{
			return _errorLineNum;
		}
		void PrintError() const;

		void Clear();

		void DeepCopy(XMLDocument* target) const;

		char* Identify(char* p, XMLNode** node);

		void MarkInUse(XMLNode*);

		virtual XMLNode* ShallowClone(XMLDocument*  ) const {
			return 0;
		}
		virtual bool ShallowEqual(const XMLNode*  ) const {
			return false;
		}

	private:
		XMLDocument(const XMLDocument&);
		void operator=(const XMLDocument&);

		bool			_writeBOM;
		bool			_processEntities;
		XMLError		_errorID;
		Whitespace		_whitespaceMode;
		mutable StrPair	_errorStr1;
		mutable StrPair	_errorStr2;
		int             _errorLineNum;
		char*			_charBuffer;
		int				_parseCurLineNum;
		DynArray<XMLNode*, 10> _unlinked;

		MemPoolT< sizeof(XMLElement) >	 _elementPool;
		MemPoolT< sizeof(XMLAttribute) > _attributePool;
		MemPoolT< sizeof(XMLText) >		 _textPool;
		MemPoolT< sizeof(XMLComment) >	 _commentPool;

		static const char* _errorNames[XML_ERROR_COUNT];

		void Parse();

		template<class NodeType, int PoolElementSize>
		NodeType* CreateUnlinkedNode(MemPoolT<PoolElementSize>& pool);
	};

	template<class NodeType, int PoolElementSize>
	inline NodeType* XMLDocument::CreateUnlinkedNode(MemPoolT<PoolElementSize>& pool)
	{
		TIXMLASSERT(sizeof(NodeType) == PoolElementSize);
		TIXMLASSERT(sizeof(NodeType) == pool.ItemSize());
		NodeType* returnNode = new (pool.Alloc()) NodeType(this);
		TIXMLASSERT(returnNode);
		returnNode->_memPool = &pool;

		_unlinked.Push(returnNode);
		return returnNode;
	}

	class TINYXML2_LIB XMLHandle
	{
	public:
		XMLHandle(XMLNode* node) : _node(node) {
		}
		XMLHandle(XMLNode& node) : _node(&node) {
		}
		XMLHandle(const XMLHandle& ref) : _node(ref._node) {
		}
		XMLHandle& operator=(const XMLHandle& ref) {
			_node = ref._node;
			return *this;
		}

		XMLHandle FirstChild() {
			return XMLHandle(_node ? _node->FirstChild() : 0);
		}
		XMLHandle FirstChildElement(const char* name = 0) {
			return XMLHandle(_node ? _node->FirstChildElement(name) : 0);
		}
		XMLHandle LastChild() {
			return XMLHandle(_node ? _node->LastChild() : 0);
		}
		XMLHandle LastChildElement(const char* name = 0) {
			return XMLHandle(_node ? _node->LastChildElement(name) : 0);
		}
		XMLHandle PreviousSibling() {
			return XMLHandle(_node ? _node->PreviousSibling() : 0);
		}
		XMLHandle PreviousSiblingElement(const char* name = 0) {
			return XMLHandle(_node ? _node->PreviousSiblingElement(name) : 0);
		}
		XMLHandle NextSibling() {
			return XMLHandle(_node ? _node->NextSibling() : 0);
		}
		XMLHandle NextSiblingElement(const char* name = 0) {
			return XMLHandle(_node ? _node->NextSiblingElement(name) : 0);
		}

		XMLNode* ToNode() {
			return _node;
		}
		XMLElement* ToElement() {
			return (_node ? _node->ToElement() : 0);
		}
		XMLText* ToText() {
			return (_node ? _node->ToText() : 0);
		}
		XMLUnknown* ToUnknown() {
			return (_node ? _node->ToUnknown() : 0);
		}
		XMLDeclaration* ToDeclaration() {
			return (_node ? _node->ToDeclaration() : 0);
		}

	private:
		XMLNode* _node;
	};

	class TINYXML2_LIB XMLConstHandle
	{
	public:
		XMLConstHandle(const XMLNode* node) : _node(node) {
		}
		XMLConstHandle(const XMLNode& node) : _node(&node) {
		}
		XMLConstHandle(const XMLConstHandle& ref) : _node(ref._node) {
		}

		XMLConstHandle& operator=(const XMLConstHandle& ref) {
			_node = ref._node;
			return *this;
		}

		const XMLConstHandle FirstChild() const {
			return XMLConstHandle(_node ? _node->FirstChild() : 0);
		}
		const XMLConstHandle FirstChildElement(const char* name = 0) const {
			return XMLConstHandle(_node ? _node->FirstChildElement(name) : 0);
		}
		const XMLConstHandle LastChild()	const {
			return XMLConstHandle(_node ? _node->LastChild() : 0);
		}
		const XMLConstHandle LastChildElement(const char* name = 0) const {
			return XMLConstHandle(_node ? _node->LastChildElement(name) : 0);
		}
		const XMLConstHandle PreviousSibling() const {
			return XMLConstHandle(_node ? _node->PreviousSibling() : 0);
		}
		const XMLConstHandle PreviousSiblingElement(const char* name = 0) const {
			return XMLConstHandle(_node ? _node->PreviousSiblingElement(name) : 0);
		}
		const XMLConstHandle NextSibling() const {
			return XMLConstHandle(_node ? _node->NextSibling() : 0);
		}
		const XMLConstHandle NextSiblingElement(const char* name = 0) const {
			return XMLConstHandle(_node ? _node->NextSiblingElement(name) : 0);
		}

		const XMLNode* ToNode() const {
			return _node;
		}
		const XMLElement* ToElement() const {
			return (_node ? _node->ToElement() : 0);
		}
		const XMLText* ToText() const {
			return (_node ? _node->ToText() : 0);
		}
		const XMLUnknown* ToUnknown() const {
			return (_node ? _node->ToUnknown() : 0);
		}
		const XMLDeclaration* ToDeclaration() const {
			return (_node ? _node->ToDeclaration() : 0);
		}

	private:
		const XMLNode* _node;
	};

	class TINYXML2_LIB XMLPrinter : public XMLVisitor
	{
	public:
		XMLPrinter(FILE* file = 0, bool compact = false, int depth = 0);
		virtual ~XMLPrinter() {}

		void PushHeader(bool writeBOM, bool writeDeclaration);
		void OpenElement(const char* name, bool compactMode = false);
		void PushAttribute(const char* name, const char* value);
		void PushAttribute(const char* name, int value);
		void PushAttribute(const char* name, unsigned value);
		void PushAttribute(const char* name, int64_t value);
		void PushAttribute(const char* name, bool value);
		void PushAttribute(const char* name, double value);
		virtual void CloseElement(bool compactMode = false);

		void PushText(const char* text, bool cdata = false);
		void PushText(int value);
		void PushText(unsigned value);
		void PushText(int64_t value);
		void PushText(bool value);
		void PushText(float value);
		void PushText(double value);

		void PushComment(const char* comment);

		void PushDeclaration(const char* value);
		void PushUnknown(const char* value);

		virtual bool VisitEnter(const XMLDocument&  );
		virtual bool VisitExit(const XMLDocument&  ) {
			return true;
		}

		virtual bool VisitEnter(const XMLElement& element, const XMLAttribute* attribute);
		virtual bool VisitExit(const XMLElement& element);

		virtual bool Visit(const XMLText& text);
		virtual bool Visit(const XMLComment& comment);
		virtual bool Visit(const XMLDeclaration& declaration);
		virtual bool Visit(const XMLUnknown& unknown);

		const char* CStr() const {
			return _buffer.Mem();
		}
		int CStrSize() const {
			return _buffer.Size();
		}
		void ClearBuffer() {
			_buffer.Clear();
			_buffer.Push(0);
			_firstElement = true;
		}

	protected:
		virtual bool CompactMode(const XMLElement&) { return _compactMode; }

		virtual void PrintSpace(int depth);
		void Print(const char* format, ...);

		void SealElementIfJustOpened();
		bool _elementJustOpened;
		DynArray< const char*, 10 > _stack;

	private:
		void PrintString(const char*, bool restrictedEntitySet);

		bool _firstElement;
		FILE* _fp;
		int _depth;
		int _textDepth;
		bool _processEntities;
		bool _compactMode;

		enum {
			ENTITY_RANGE = 64,
			BUF_SIZE = 200
		};
		bool _entityFlag[ENTITY_RANGE];
		bool _restrictedEntityFlag[ENTITY_RANGE];

		DynArray< char, 20 > _buffer;

		XMLPrinter(const XMLPrinter&);
		XMLPrinter& operator=(const XMLPrinter&);
	};

}

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif
