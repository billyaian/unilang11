﻿// © 2020-2020 Uniontech Software Technology Co.,Ltd.

#include <memory_resource> // for complete std::pmr::polymorphic_allocator;
#include <list> // for std::pmr::list;
#include <deque> // for std::pmr::deque;
#include <stack> // for std::stack;
#include <string> // for std::pmr::string, std::getline;
#include <string_view> // for std::string_view;
#include <vector> // for std::pmr::vector;
#include <exception> // for std::runtime_error;
#include <cctype> // for std::isgraph;
#include <cassert> // for assert;
#include <any> // for std::any;
#include <utility> // for std::ref;
#include <algorithm> // for std::for_each;
#include <iostream>
#include <typeinfo> // for typeid;

namespace Unilang
{

using std::pmr::list;
template<typename _type, class _tSeqCon = std::pmr::deque<_type>>
using stack = std::stack<_type, _tSeqCon>;
using std::pmr::string;
using std::string_view;
using std::pmr::vector;

} // namespace Unilang;

namespace
{

using namespace Unilang;

class UnilangException : public std::runtime_error
{
	using runtime_error::runtime_error;
};


using ValueObject = std::any;


[[nodiscard]] constexpr bool
IsGraphicalDelimiter(char c) noexcept
{
	return c == '(' || c == ')' || c == ',' || c == ';';
}

[[nodiscard]] constexpr bool
IsDelimiter(char c) noexcept
{
#if CHAR_MIN < 0
	return c >= 0 && (!std::isgraph(c) || IsGraphicalDelimiter(c));
#else
	return !std::isgraph(c) || IsGraphicalDelimiter(c);
#endif
}


class ByteParser final
{
public:
	using ParseResult = vector<string>;

private:
	char Delimiter = {};
	string buffer{};
	mutable ParseResult lexemes{};
	bool update_current = {};

public:
	void
	operator()(char c)
	{
		buffer += c;
		Update(UpdateBack(buffer.back(), c));
	}

	const ParseResult&
	GetResult() const noexcept
	{
		return lexemes;
	}

private:
	void
	Update(bool got_delim)
	{
		[&](auto add, auto append){
			const auto len(buffer.length());

			assert(!(lexemes.empty() && update_current));
			if(len > 0)
			{
				if(len == 1)
				{
					const auto update_c([&](char c){
						if(update_current)
							append(c);
						else
							add(string({c}, lexemes.get_allocator()));
					});
					const char c(buffer.back());
					const bool unquoted(Delimiter == char());

 					if(got_delim)
					{
						update_c(c);
						update_current = !unquoted;
					}
					else if(unquoted && IsDelimiter(c))
					{
						if(IsGraphicalDelimiter(c))
							add(string({c}, lexemes.get_allocator()));
						update_current = {};
					}
					else
					{
						update_c(c);
						update_current = true;
					}
				}
				else if(update_current)
					append(buffer.substr(0, len));
				else
					add(std::move(buffer));
				buffer.clear();
			}
		}([&](auto&& arg){
			lexemes.push_back(std::forward<decltype(arg)>(arg));
		}, [&](auto&& arg){
			lexemes.back() += std::forward<decltype(arg)>(arg);
		});
	}

	bool
	UpdateBack(char& b, char c)
	{
		switch(c)
		{
			case '\'':
			case '"':
				if(Delimiter == char())
				{
					Delimiter = c;
					return true;
				}
				else if(Delimiter == c)
				{
					Delimiter = char();
					return true;
				}
				break;
			case '\f':
			case '\n':
			case '\t':
			case '\v':
				if(Delimiter == char())
				{
					b = ' ';
					break;
				}
				[[fallthrough]];
			default:
				break;
		}
		return {};
	}
};


class TermNode final
{
public:
	using Container = list<TermNode>;
	using allocator_type = Container::allocator_type;
	using iterator = Container::iterator;
	using const_iterator = Container::const_iterator;
	using reverse_iterator = Container::reverse_iterator;
	using const_reverse_iterator = Container::const_reverse_iterator;

	Container Subterms{};
	ValueObject Value{};

	TermNode() = default;
	explicit
	TermNode(allocator_type a)
		: Subterms(a)
	{}
	TermNode(const Container& con)
		: Subterms(con)
	{}
	TermNode(Container&& con)
		: Subterms(std::move(con))
	{}
	TermNode(const TermNode&) = default;
	TermNode(const TermNode& tm, allocator_type a)
		: Subterms(tm.Subterms, a), Value(tm.Value)
	{}
	TermNode(TermNode&&) = default;
	TermNode(TermNode&& tm, allocator_type a)
		: Subterms(std::move(tm.Subterms), a), Value(std::move(tm.Value))
	{}
	
	TermNode&
	operator=(TermNode&&) = default;

	void
	Add(const TermNode& term)
	{
		Subterms.push_back(term);
	}
	void
	Add(TermNode&& term)
	{
		Subterms.push_back(std::move(term));
	}

	[[nodiscard, gnu::pure]]
	allocator_type
	get_allocator() const noexcept
	{
		return Subterms.get_allocator();
	}
};


template<typename... _tParams>
[[nodiscard, gnu::pure]] inline TermNode
AsTermNode(_tParams&&... args)
{
	TermNode term;

	term.Value = ValueObject(std::forward<_tParams>(args)...);
	return term;
}


struct LexemeTokenizer final
{
	// XXX: Allocator is ignored;
	LexemeTokenizer(TermNode::allocator_type = {})
	{}

	template<class _type>
	[[nodiscard, gnu::pure]] TermNode
	operator()(const _type& val) const
	{
		return AsTermNode(val);
	}
};

template<typename _tIn>
[[nodiscard]] inline _tIn
ReduceSyntax(TermNode& term, _tIn first, _tIn last)
{
	return ReduceSyntax(term, first, last,
		LexemeTokenizer{term.get_allocator()});
}
template<typename _tIn, typename _fTokenize>
[[nodiscard]] _tIn
ReduceSyntax(TermNode& term, _tIn first, _tIn last, _fTokenize tokenize)
{
	const auto a(term.get_allocator());
	stack<TermNode> tms(a);
	struct Guard final
	{
		TermNode& term;
		stack<TermNode>& tms;

		~Guard()
		{
			assert(!tms.empty());
			term = std::move(tms.top());
		}
	} gd{term, tms};

	tms.push(std::move(term));
	for(; first != last; ++first)
		if(*first == "(")
			tms.push(AsTermNode());
		else if(*first != ")")
		{
			assert(!tms.empty());
			tms.top().Add(tokenize(*first));
		}
		else if(tms.size() != 1)
		{
			auto tm(std::move(tms.top()));

			tms.pop();
			assert(!tms.empty());
			tms.top().Add(std::move(tm));
		}
		else
			return first;
	if(tms.size() == 1)
		return first;
	throw UnilangException("Redundant '(' found.");
}


class Interpreter final
{
private:
	string line{};

public:
	Interpreter();
	Interpreter(const Interpreter&) = delete;

	void
	Evaluate(TermNode&);

	void
	Print(const TermNode&);

	bool
	Process();

	[[nodiscard]] TermNode
	Read(string_view);

	void
	Run();

	std::istream&
	WaitForLine();
};

Interpreter::Interpreter()
{}

void
Interpreter::Evaluate(TermNode& term)
{
}

void
Interpreter::Print(const TermNode&)
{
}

bool
Interpreter::Process()
{
	if(line == "exit")
		return {};
	else if(!line.empty())
		try
		{
			auto term(Read(line));

			Evaluate(term);
			Print(term);
		}
		catch(UnilangException& e)
		{
			using namespace std;

			cerr << "UnilangException[" << typeid(e).name() << "]: "
				<< e.what() << endl;
		}
	return true;
}

TermNode
Interpreter::Read(string_view unit)
{
	ByteParser parse{};

	std::for_each(unit.begin(), unit.end(), std::ref(parse));

	const auto& parse_result(parse.GetResult());
	TermNode term{};

	if(ReduceSyntax(term, parse_result.cbegin(), parse_result.cend())
		!= parse_result.cend())
		throw UnilangException("Redundant ')' found.");
	return term;
}

void
Interpreter::Run()
{
	while(WaitForLine() && Process())
		;
}

std::istream&
Interpreter::WaitForLine()
{
	using namespace std;

	cout << "> ";
	return getline(cin, line);
}


#define APP_NAME "Unilang demo"
#define APP_VER "0.0.5"
#define APP_PLATFORM "[C++17]"
constexpr auto
	title(APP_NAME " " APP_VER " @ (" __DATE__ ", " __TIME__ ") " APP_PLATFORM);

} // unnamed namespace;

int
main()
{
	using namespace std;
	Interpreter intp{};

	cout << title << endl << "Initializing...";
	cout << "Initialization finished." << endl;
	cout << "Type \"exit\" to exit, \"cls\" to clear screen." << endl << endl;
	intp.Run();
}

