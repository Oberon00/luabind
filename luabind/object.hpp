// Copyright (c) 2003 Daniel Wallin and Arvid Norberg

// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

#if !BOOST_PP_IS_ITERATING

#ifndef LUABIND_OBJECT_HPP_INCLUDED
#define LUABIND_OBJECT_HPP_INCLUDED

#include <iterator>

#include <luabind/config.hpp>
#include <luabind/detail/error.hpp>

#include <boost/preprocessor/repeat.hpp>
#include <boost/preprocessor/iteration/iterate.hpp>
#include <boost/preprocessor/repetition/enum.hpp> 
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_binary_params.hpp>
#include <boost/tuple/tuple.hpp>

namespace luabind
{
	// object might need to be able to store values temporarily
	// without knowing about a lua_State.
	//
	// globals["f"] = function(&f);
	//
	// Creates the need for this, since function() doesn't know
	// about the state and thus can't return a fully initialized
	// object.
	//
	// Current solution is to allow for objects to store a pointer
	// to a commiter-object. This object has a virtual method which
	// converts it's value to the lua_State.
	//
	// This has some serious issues. For example, how would
	// two 'pseude-initialized' objects be able to compare?
	//
	// Perhaps attempting to perform operations on non-intialised
	// objects could throw?
	// 
	// Perhaps we could perform some not-so-smart comparisions? like:
	//
	//	template<class T>
	// struct commiter : base_commiter
	// {
	//		commiter(const T& v): val(v), base_commiter(typeid(T)) {}
	//
	//		virtual bool compare(void* rhs)
	//		{
	//			T* other = static_cast<T*>(rhs);
	//			return val == *other;
	//		}
	//
	//		T val;
	// };
	//
	// This would at least allow for the most intuitive use.. like:
	//
	// object a = 5;
	// object b = 5;
	//
	// return a == b;
	//
	// However, comparing an initialized object with a non-initialized
	// would always return false. Is this ok? Better to disallow it?

	// the current implementation does not have commiters, all objects
	// knows about the lua_State* or is uninitialized.

	class object;

	namespace detail
	{
		class proxy_object;
		class proxy_raw_object;
		class proxy_array_object;

		template<class T>
		void convert_to_lua(lua_State*, const T&);

		template<int Index, class T, class Policies>
		void convert_to_lua_p(lua_State*, const T&, const Policies&);

		template<int Index>
		struct push_args_from_tuple
		{
			template<class H, class T, class Policies>
			inline static void apply(lua_State* L, const boost::tuples::cons<H, T>& x, const Policies& p) 
			{
				convert_to_lua_p<Index>(L, *x.get_head(), p);
				push_args_from_tuple<Index+1>::apply(L, x.get_tail(), p);
			}

			template<class H, class T>
			inline static void apply(lua_State* L, const boost::tuples::cons<H, T>& x) 
			{
				convert_to_lua(L, *x.get_head());
				push_args_from_tuple<Index+1>::apply(L, x.get_tail());
			}

			template<class Policies>
			inline static void apply(lua_State*, const boost::tuples::null_type&, const Policies&) {};

			inline static void apply(lua_State*, const boost::tuples::null_type&) {};

		};

		template<class Tuple>
		class proxy_caller
		{
		friend class luabind::object;
		public:

			proxy_caller(luabind::object* o, const Tuple args)
				: m_obj(o)
				, m_args(args)
				, m_called(false)
			{
			}

			proxy_caller(const detail::proxy_caller<Tuple>& rhs)
				: m_obj(rhs.m_obj)
				, m_args(rhs.m_args)
				, m_called(rhs.m_called)
			{
				rhs.m_called = true;
			}

			~proxy_caller();
			operator luabind::object();

#if defined(BOOST_MSVC) && (BOOST_MSVC <= 1300)
	#define SEMICOLON
#else
	#define SEMICOLON ;
#endif

			template<class Policies>
			luabind::object operator[](const Policies& p) SEMICOLON
#if defined(BOOST_MSVC) && (BOOST_MSVC <= 1300)
			{
				m_called = true;
				lua_State* L = m_obj->lua_state();
				m_obj->pushvalue();
				detail::push_args_from_tuple<1>::apply(L, m_args, p);
				if (lua_pcall(L, boost::tuples::length<Tuple>::value, 1, 0))
				{ 
#ifndef LUABIND_NO_EXCEPTIONS
					throw error();
#else
					assert(0);
#endif
				}
				int ref = detail::ref(L);
				return luabind::object(m_obj->lua_state(), ref, true/*luabind::object::reference()*/);
			}
#endif


#undef SEMICOLON
		private:

			luabind::object* m_obj;
			Tuple m_args;
			mutable bool m_called;

		};



		struct stack_pop
		{
			stack_pop(lua_State* L, int n)
				: m_state(L)
				, m_n(n)
				{
				}

			~stack_pop() 
			{
				lua_pop(m_state, m_n);
			}

		private:

			lua_State* m_state;
			int m_n;
		};





		class proxy_object
		{
		friend class luabind::object;
		friend class luabind::detail::proxy_array_object;
		friend class luabind::detail::proxy_raw_object;
//		template<class T> friend T object_cast(const proxy_object& obj);
		public:

			template<class T>
			proxy_object& operator=(const T& val)
			{
				//std::cout << "proxy assigment\n";
				lua_State* L = m_obj->m_state;
				m_obj->pushvalue();
				detail::getref(L, m_key_ref);
				detail::convert_to_lua(L, val);
				lua_settable(L, -3);
				// pop table
				lua_pop(L, 1);
				return *this;
			}

			proxy_object& operator=(const proxy_object& p);
			proxy_object& operator=(const proxy_raw_object& p);
			proxy_object& operator=(const proxy_array_object& p);

			operator luabind::object();

			lua_State* lua_state() const;
			void pushvalue() const;

		private:

			proxy_object(luabind::object* o, int key)
				: m_obj(o)
				, m_key_ref(key)
			{
			}

			luabind::object* m_obj;
			int m_key_ref;
		};



		class proxy_raw_object
		{
		friend class luabind::object;
		friend class luabind::detail::proxy_array_object;
		friend class luabind::detail::proxy_object;
//		template<class T> friend T luabind::object_cast(const proxy_object& obj);
		public:

			template<class T>
			proxy_raw_object& operator=(const T& val)
			{
				//std::cout << "proxy assigment\n";
				lua_State* L = m_obj->m_state;
				m_obj->pushvalue();
				detail::getref(L, m_key_ref);
				detail::convert_to_lua(L, val);
				lua_rawset(L, -3);
				// pop table
				lua_pop(L, 1);
				return *this;
			}

			proxy_raw_object& operator=(const proxy_object& p);
			proxy_raw_object& operator=(const proxy_raw_object& p);
			proxy_raw_object& operator=(const proxy_array_object& p);

			operator luabind::object();

			lua_State* lua_state() const;
			void pushvalue() const;

		private:

			proxy_raw_object(luabind::object* o, int key)
				: m_obj(o)
				, m_key_ref(key)
			{
			}

			luabind::object* m_obj;
			int m_key_ref;
		};



		class proxy_array_object
		{
		friend class luabind::object;
		friend class luabind::detail::proxy_object;
		friend class luabind::detail::proxy_raw_object;
//		template<class T> friend T object_cast(const proxy_array_object& obj);
		public:

			template<class T>
			proxy_array_object& operator=(const T& val)
			{
				//std::cout << "array proxy assigment\n";
				lua_State* L = m_obj->m_state;
				m_obj->pushvalue();
				detail::convert_to_lua(L, val);
				lua_rawseti(L, -2, m_key);

				// pops the table
				lua_pop(L, 1);
				return *this;
			}

			proxy_array_object& operator=(const proxy_object& p);
			proxy_array_object& operator=(const proxy_raw_object& p);
			proxy_array_object& operator=(const proxy_array_object& p);

			operator luabind::object();

			lua_State* lua_state() const;
			void pushvalue() const;

		private:

			proxy_array_object(luabind::object* o, int key)
				: m_obj(o)
				, m_key(key)
			{
			}
			luabind::object* m_obj;
			int m_key;
		};

		template<class T>
		struct primitive_converter;

	} // detail




	class object
	{

#if !(defined (BOOST_MSVC) && (BOOST_MSVC <= 1200))

	template<class T>
	friend T object_cast(const object& obj);
	template<class T>
	friend struct detail::primitive_converter;

#endif

	friend object get_globals(lua_State*);
	friend object get_registry(lua_State*);
	friend object newtable(lua_State*);
	friend class detail::proxy_object;
	friend class detail::proxy_array_object;
	friend class detail::proxy_raw_object;

	public:

		class array_iterator
		{
		friend class object;
		public:

			typedef std::forward_iterator_tag iterator_category;
			typedef luabind::object value_type;
		    typedef value_type& reference;
		    typedef value_type* pointer;
			typedef void difference_type;

			array_iterator()
				: m_obj(0)
				, m_key(LUA_NOREF)
			{
			}

			array_iterator(const array_iterator& iter)
				: m_obj(iter.m_obj)
				, m_key(iter.m_key)
			{
			}

			~array_iterator() {}

			array_iterator& operator=(const array_iterator& rhs)
			{
				//std::cout << "===\n";
				m_obj = rhs.m_obj;
				m_key = rhs.m_key;
				return *this;
			}

			detail::proxy_array_object operator*()
			{
				return m_obj->make_array_proxy(m_key);
			}

			inline array_iterator& operator++()
			{
				m_key++;

				// invalidate the iterator if we hit a nil element
				lua_State* L = m_obj->lua_state();
				m_obj->pushvalue();
				lua_rawgeti(L, -1, m_key);
				if (lua_isnil(L, -1)) m_key = LUA_NOREF;
				lua_pop(L, 1);

				return *this;
			}

			inline array_iterator operator++(int)
			{
				int old_key = m_key;
				m_key++;

				// invalidate the iterator if we hit a nil element
				lua_State* L = m_obj->lua_state();
				m_obj->pushvalue();
				lua_rawgeti(L, -1, m_key);
				if (lua_isnil(L, -1)) m_key = LUA_NOREF;
				lua_pop(L, 1);

				return array_iterator(m_obj, old_key);
			}

			bool operator!=(const array_iterator& rhs) const
			{
				return m_obj != rhs.m_obj || m_key != rhs.m_key;
			}

		private:

			array_iterator(object* obj, int key)
				: m_obj(obj)
				, m_key(key)
			{
			}

			object* m_obj;
			int m_key;
		};






		class iterator
		{
		friend class object;
		public:

			typedef std::forward_iterator_tag iterator_category;
			typedef luabind::object value_type;
		    typedef value_type& reference;
		    typedef value_type* pointer;
			typedef void difference_type;

			iterator()
				: m_obj(0)
				, m_key(LUA_NOREF)
			{
			}

			iterator(const iterator& iter)
				: m_obj(iter.m_obj)
				, m_key(LUA_NOREF)
			{
				if (m_obj)
				{
					lua_State* L = m_obj->lua_state();
					detail::getref(L, iter.m_key);
					m_key = detail::ref(L);
				}
			}

			~iterator()
			{
				if (m_obj && m_key != LUA_NOREF) detail::unref(m_obj->lua_state(), m_key);
			}

			iterator& operator=(const iterator& rhs)
			{
				//std::cout << "===\n";
				m_obj = rhs.m_obj;
				if (m_obj)
				{
					lua_State* L = m_obj->lua_state();
					detail::getref(L, rhs.m_key);
					m_key = detail::ref(L);
				}
				else
				{
					m_key = LUA_NOREF;
				}
				return *this;
			}

			detail::proxy_object operator*()
			{
				return m_obj->make_proxy(m_key);
			}

			iterator& operator++()
			{
				lua_State* L = m_obj->lua_state();
				m_obj->pushvalue();
				detail::getref(L, m_key);

				if (lua_next(L, -2) != 0)
				{
					lua_pop(L, 1);
					lua_rawseti(L, LUA_REGISTRYINDEX, m_key);
					lua_pop(L, 1);
				}
				else
				{
					lua_pop(L, 1);
					detail::unref(L, m_key);
					m_obj = 0;
					m_key = LUA_NOREF;
				}

				return *this;
			}

			bool operator!=(const iterator& rhs) const
			{
				return m_obj != rhs.m_obj || m_key != rhs.m_key;
			}

		private:

			iterator(object* obj, int key)
				: m_obj(obj)
				, m_key(key)
			{
			}

			object* m_obj;
			int m_key;
		};




		class raw_iterator
		{
		friend class object;
		public:

			typedef std::forward_iterator_tag iterator_category;
			typedef luabind::object value_type;
		    typedef value_type& reference;
		    typedef value_type* pointer;
			typedef void difference_type;

			raw_iterator()
				: m_obj(0)
				, m_key(LUA_NOREF)
			{
			}

			raw_iterator(const raw_iterator& iter)
				: m_obj(iter.m_obj)
				, m_key(LUA_NOREF)
			{
				if (m_obj)
				{
					lua_State* L = m_obj->lua_state();
					detail::getref(L, iter.m_key);
					m_key = detail::ref(L);
				}
			}

			~raw_iterator()
			{
				if (m_obj && m_key != LUA_NOREF) detail::unref(m_obj->lua_state(), m_key);
			}

			raw_iterator& operator=(const raw_iterator& rhs)
			{
				//std::cout << "===\n";
				m_obj = rhs.m_obj;
				if (m_obj)
				{
					lua_State* L = m_obj->lua_state();
					detail::getref(L, rhs.m_key);
					m_key = detail::ref(L);
				}
				else
				{
					m_key = LUA_NOREF;
				}
				return *this;
			}

			detail::proxy_raw_object operator*()
			{
				return m_obj->make_raw_proxy(m_key);
			}

			raw_iterator& operator++()
			{
				lua_State* L = m_obj->lua_state();
				m_obj->pushvalue();
				detail::getref(L, m_key);

				if (lua_next(L, -2) != 0)
				{
					lua_pop(L, 1);
					lua_rawseti(L, LUA_REGISTRYINDEX, m_key);
					lua_pop(L, 1);
				}
				else
				{
					lua_pop(L, 1);
					detail::unref(L, m_key);
					m_obj = 0;
					m_key = LUA_NOREF;
				}

				return *this;
			}

			bool operator!=(const raw_iterator& rhs) const
			{
				return m_obj != rhs.m_obj || m_key != rhs.m_key;
			}

		private:

			raw_iterator(object* obj, int key)
				: m_obj(obj)
				, m_key(key)
			{
			}

			object* m_obj;
			int m_key;
		};






		object()
			: m_state(0)
			, m_ref(LUA_NOREF)
		{
		}

		object(lua_State* L)
			: m_state(L)
			, m_ref(LUA_NOREF)
		{
		}

		template<class T>
		object(lua_State* L, const T& val)
			: m_state(L)
			, m_ref(LUA_NOREF)
		{
			*this = val;
		}

		object(const object& o)
			: m_state(o.m_state)
			, m_ref(LUA_NOREF)
		{
			lua_getref(m_state, o.m_ref);
			m_ref = detail::ref(m_state);
		}

		inline ~object()
		{
			// If you crash in the detail::unref() call you have probably
			// closed the lua_State before destructing all object instances.
			if (m_ref != LUA_NOREF) detail::unref(m_state, m_ref);
		}

		inline void swap(object& rhs)
		{
			// you cannot swap objects from different lua states
			assert(m_state == rhs.m_state);
			std::swap(m_ref, rhs.m_ref);
		}

		inline bool is_valid() const { return m_ref != LUA_NOREF; }

		// this is a safe substitute for an implicit converter to bool
		typedef void (object::*member_ptr)() const;
		operator member_ptr() const
		{
			if (is_valid()) return &object::dummy;
			return 0;
		}

		int type() const
		{
			lua_getref(m_state, m_ref);
			detail::stack_pop p(m_state, 1);
			return lua_type(m_state, -1);
		}

		inline iterator begin() const
		{
			lua_getref(m_state, m_ref);
			lua_pushnil(m_state);
			lua_next(m_state, -2);
			lua_pop(m_state, 1);
			iterator i(const_cast<object*>(this), detail::ref(m_state));
			lua_pop(m_state, 1);
			return i;
		}

		inline iterator end() const
		{
			return iterator(0, LUA_NOREF);
		}

		inline array_iterator abegin() const
		{
			// TODO: This should start counting at 1, right?
			return array_iterator(const_cast<object*>(this), 1);
		}

		inline array_iterator aend() const
		{
			return array_iterator(const_cast<object*>(this), LUA_NOREF);
		}

		raw_iterator raw_begin() const
		{
			lua_getref(m_state, m_ref);
			lua_pushnil(m_state);
			lua_next(m_state, -2);
			lua_pop(m_state, 1);
			raw_iterator i(const_cast<object*>(this), detail::ref(m_state));
			lua_pop(m_state, 1);
			return i;
		}

		raw_iterator raw_end() const
		{
			return raw_iterator(0, LUA_NOREF);
		}

		inline lua_State* lua_state() const { return m_state; }
		inline void pushvalue() const
		{
			// you are trying to dereference an invalid object
			assert(m_ref != LUA_NOREF);
			assert(m_state != 0);

			lua_getref(m_state, m_ref);
		}


		template<class T>
		inline object raw_at(const T& key)
		{
			pushvalue();
			detail::convert_to_lua(m_state, key);
			lua_rawget(m_state, -2);
			int ref = detail::ref(m_state);
			lua_pop(m_state, 1);
			return object(m_state, ref, true);
		}

		template<class T>
		inline object at(const T& key)
		{
			pushvalue();
			detail::convert_to_lua(m_state, key);
			lua_gettable(m_state, -2);
			int ref = detail::ref(m_state);
			lua_pop(m_state, 1);
			return object(m_state, ref, true);
		}

		template<class T>
		inline detail::proxy_object operator[](const T& key) const
		{
			detail::convert_to_lua(m_state, key);
			int ref = detail::ref(m_state);
			return detail::proxy_object(const_cast<object*>(this), ref);
		}


		template<class T>
		object operator+(const T& rhs) const;

		template<class T>
		object operator-(const T& rhs) const;

		template<class T>
		object operator*(const T& rhs) const;

		template<class T>
		object operator/(const T& rhs) const;


		// *****************************
		// OPERATOR =


		object& operator=(const object& o) const
		{
			m_state = o.lua_state();
			allocate_slot();
			o.pushvalue();
			lua_rawseti(m_state, LUA_REGISTRYINDEX, m_ref);
			return const_cast<luabind::object&>(*this);
		}

		object& operator=(const detail::proxy_object& o) const
		{
			m_state = o.lua_state();
			allocate_slot();
			o.pushvalue();
			lua_rawseti(m_state, LUA_REGISTRYINDEX, m_ref);
			return const_cast<luabind::object&>(*this);
		}

		object& operator=(const detail::proxy_raw_object& o) const
		{
			m_state = o.lua_state();
			allocate_slot();
			o.pushvalue();
			lua_rawseti(m_state, LUA_REGISTRYINDEX, m_ref);
			return const_cast<luabind::object&>(*this);
		}

		object& operator=(const detail::proxy_array_object& o) const
		{
			m_state = o.lua_state();
			allocate_slot();
			o.pushvalue();
			lua_rawseti(m_state, LUA_REGISTRYINDEX, m_ref);
			return const_cast<luabind::object&>(*this);
		}

		template<class T>
		object& operator=(const T& val) const
		{
			assert(m_state != 0); // you cannot assign a non-lua value to an uninitialized object

			allocate_slot();
			detail::convert_to_lua(m_state, val);
			lua_rawseti(m_state, LUA_REGISTRYINDEX, m_ref);
			return const_cast<luabind::object&>(*this);
		}


		// *****************************
		// OPERATOR ==


		bool operator==(const object& rhs) const
		{
			if (m_ref == LUA_NOREF || rhs.m_ref == LUA_NOREF) return false;
			lua_getref(m_state, m_ref);
			rhs.pushvalue();
			bool result = lua_equal(m_state, -1, -2) != 0;
			lua_pop(m_state, 2);
			return result;
		}

		bool operator==(const detail::proxy_object& rhs) const
		{
			if (m_ref == LUA_NOREF) return false;
			lua_getref(m_state, m_ref);
			rhs.pushvalue();
			bool result = lua_equal(m_state, -1, -2) != 0;
			lua_pop(m_state, 2);
			return result;
		}

		bool operator==(const detail::proxy_raw_object& rhs) const
		{
			if (m_ref == LUA_NOREF) return false;
			lua_getref(m_state, m_ref);
			rhs.pushvalue();
			bool result = lua_equal(m_state, -1, -2) != 0;
			lua_pop(m_state, 2);
			return result;
		}

		bool operator==(const detail::proxy_array_object& rhs) const
		{
			if (m_ref == LUA_NOREF) return false;
			lua_getref(m_state, m_ref);
			rhs.pushvalue();
			bool result = lua_equal(m_state, -1, -2) != 0;
			lua_pop(m_state, 2);
			return result;
		}

		template<class T>
		bool operator==(const T& rhs) const
		{
			if (m_ref == LUA_NOREF) return false;
			lua_getref(m_state, m_ref);
			detail::convert_to_lua(m_state, rhs);

			bool result = lua_equal(m_state, -1, -2);

			lua_pop(m_state, 2);
			return result;
		}



		// *****************************
		// OPERATOR()

		#define BOOST_PP_ITERATION_PARAMS_1 (4, (0, LUABIND_MAX_ARITY, <luabind/object.hpp>, 1))
		#include BOOST_PP_ITERATE()



		inline detail::proxy_object make_proxy(int key)
		{
			return detail::proxy_object(this, key);
		}

		inline detail::proxy_raw_object make_raw_proxy(int key)
		{
			return detail::proxy_raw_object(this, key);
		}

		inline detail::proxy_array_object make_array_proxy(int key)
		{
			return detail::proxy_array_object(this, key);
		}

		// TODO: it's not possible to make object friend with wrapped_constructor_helper::apply (since
		// it's an inner class), that's why this interface is public
//	private:
/*
		struct stack_index {};
		struct reference {};
*/
		/*
		object(lua_State* L, int index, stack_index)
			: m_state(L)
		{
			lua_pushvalue(L, index);
			m_ref = detail::ref(L);
		}
*/
		object(lua_State* L, int ref, bool/*, reference*/)
			: m_state(L)
			, m_ref(ref)
		{
		}

private:

		void dummy() const {}

		void allocate_slot() const
		{
			if (m_ref == LUA_NOREF)
			{
				lua_pushboolean(m_state, 0);
				m_ref = detail::ref(m_state);
			}
		}

		mutable lua_State* m_state;
		mutable int m_ref;
	};



	namespace detail
	{

#if !defined(BOOST_MSVC) || (defined(BOOST_MSVC) && (BOOST_MSVC > 1300))
		template<class Tuple>
		template<class Policies>
		luabind::object proxy_caller<Tuple>::operator[](const Policies& p)
		{
			m_called = true;
			lua_State* L = m_obj->lua_state();
			m_obj->pushvalue();
			detail::push_args_from_tuple<1>::apply(L, m_args, p);
			if (lua_pcall(L, boost::tuples::length<Tuple>::value, 1, 0))
			{ 
#ifndef LUABIND_NO_EXCEPTIONS
				throw error();
#else
				assert(0);
#endif
			}
			int ref = detail::ref(L);
			return luabind::object(m_obj->lua_state(), ref, true/*luabind::object::reference()*/);
		}
#endif

		// *************************************
		// PROXY OBJECT

		inline proxy_object& proxy_object::operator=(const proxy_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			//std::cout << "proxy-proxy assigment\n";
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);

			// retrive the rhs value
			p.pushvalue();

			lua_settable(L, -3);
			lua_pop(L, 1);
			return *this;
		}

		inline proxy_object& proxy_object::operator=(const proxy_raw_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			//std::cout << "proxy-proxy assigment\n";
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);

			// retrive the rhs value
			p.pushvalue();

			lua_settable(L, -3);
			lua_pop(L, 1);
			return *this;
		}

		inline proxy_object& proxy_object::operator=(const proxy_array_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			//std::cout << "proxy-proxy assigment\n";
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);

			// retrive the rhs value
			p.m_obj->pushvalue();
			lua_rawgeti(L, -1, p.m_key);

			lua_settable(L, -3);
			lua_pop(L, 1);
			return *this;
		}

		inline void proxy_object::pushvalue() const
		{
			assert(m_key_ref != LUA_NOREF);

			lua_State* L = m_obj->lua_state();
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);
			lua_gettable(L, -2);
			// remove the table and leave the value on top of the stack
			lua_remove(L, -2);
		}

		inline lua_State* proxy_object::lua_state() const
		{
			return m_obj->lua_state();
		}

		inline proxy_object::operator luabind::object()
		{
			lua_State* L = m_obj->lua_state();
			pushvalue();
			int ref = detail::ref(L);
			return luabind::object(L, ref, true/*luabind::object::reference()*/);
		}


		// *************************************
		// PROXY ARRAY OBJECT


		inline proxy_array_object& proxy_array_object::operator=(const proxy_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			//std::cout << "proxy-proxy assigment\n";
			m_obj->pushvalue();

			// retrieve the rhs value
			p.pushvalue();

			lua_rawseti(L, -2, m_key);
			lua_pop(L, 1);
			return *this;
		}

		inline proxy_array_object& proxy_array_object::operator=(const proxy_raw_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			m_obj->pushvalue();
			p.pushvalue();

			lua_rawseti(L, -2, m_key);
			lua_pop(L, 1);
			return *this;
		}

		inline proxy_array_object& proxy_array_object::operator=(const proxy_array_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			m_obj->pushvalue();
			p.pushvalue();

			lua_rawseti(L, -2, m_key);
			lua_pop(L, 1);
			return *this;
		}

		inline lua_State* proxy_array_object::lua_state() const
		{
			return m_obj->lua_state();
		}

		inline void proxy_array_object::pushvalue() const
		{
			// you are trying to dereference an invalid object
			assert(m_key != LUA_NOREF);

			lua_State* L = m_obj->lua_state();
			m_obj->pushvalue();
			lua_rawgeti(L, -1, m_key);
			lua_remove(L, -2);
		}

		inline proxy_array_object::operator luabind::object()
		{
			lua_State* L = m_obj->lua_state();
			pushvalue();
			int ref = detail::ref(L);
			return luabind::object(L, ref, true/*luabind::object::reference()*/);
		}


		// *************************************
		// PROXY RAW OBJECT

		inline proxy_raw_object& proxy_raw_object::operator=(const proxy_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			//std::cout << "proxy-proxy assigment\n";
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);

			// retrive the rhs value
			p.pushvalue();

			lua_rawset(L, -3);
			lua_pop(L, 1);
			return *this;
		}

		inline proxy_raw_object& proxy_raw_object::operator=(const proxy_raw_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			//std::cout << "proxy-proxy assigment\n";
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);

			// retrive the rhs value
			p.pushvalue();

			lua_rawset(L, -3);
			lua_pop(L, 1);
			return *this;
		}

		inline proxy_raw_object& proxy_raw_object::operator=(const proxy_array_object& p)
		{
			// if you hit this assert you are trying to transfer values from one lua state to another
			// without first going through C++
			assert(m_obj->lua_state() == p.m_obj->lua_state());

			lua_State* L = m_obj->lua_state();

			//std::cout << "proxy-proxy assigment\n";
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);

			// retrive the rhs value
			p.m_obj->pushvalue();
			lua_rawgeti(L, -1, p.m_key);

			lua_rawset(L, -3);
			lua_pop(L, 1);
			return *this;
		}

		inline void proxy_raw_object::pushvalue() const
		{
			assert(m_key_ref >= 0);

			lua_State* L = m_obj->lua_state();
			m_obj->pushvalue();
			detail::getref(L, m_key_ref);
			lua_rawget(L, -2);
			// remove the table and leave the value on top of the stack
			lua_remove(L, -2);
		}

		inline lua_State* proxy_raw_object::lua_state() const
		{
			return m_obj->lua_state();
		}

		inline proxy_raw_object::operator luabind::object()
		{
			lua_State* L = m_obj->lua_state();
			pushvalue();
			int ref = detail::ref(L);
			return luabind::object(L, ref, true/*luabind::object::reference()*/);
		}


		// *************************************
		// PROXY CALLER


		template<class Tuple>
		proxy_caller<Tuple>::~proxy_caller()
		{
			if (m_called) return;

			m_called = true;
			lua_State* L = m_obj->lua_state();
			m_obj->pushvalue();

			push_args_from_tuple<1>::apply(L, m_args);
			if (lua_pcall(L, boost::tuples::length<Tuple>::value, 0, 0))
			{ 
#ifndef LUABIND_NO_EXCEPTIONS
				throw luabind::error();
#else
				assert(0);
#endif
			}
		}

		template<class Tuple>
		proxy_caller<Tuple>::operator luabind::object()
		{
			m_called = true;
			lua_State* L = m_obj->lua_state();
			m_obj->pushvalue();

			push_args_from_tuple<1>::apply(L, m_args);
			if (lua_pcall(L, boost::tuples::length<Tuple>::value, 1, 0))
			{ 
#ifndef LUABIND_NO_EXCEPTIONS
				throw luabind::error();
#else
				assert(0);
#endif
			}
			int ref = detail::ref(L);
			return luabind::object(m_obj->lua_state(), ref, true/*luabind::object::reference()*/);
		}

	}

}

namespace std
{
	inline void swap(luabind::object& lhs, luabind::object& rhs)
	{
		lhs.swap(rhs);
	}

	// TODO: add swap() for all proxy objects
}

#endif // LUABIND_OBJECT_HPP_INCLUDED

#elif BOOST_PP_ITERATION_FLAGS() == 1

#define LUABIND_TUPLE_PARAMS(z, n, data) const A##n *
#define LUABIND_OPERATOR_PARAMS(z, n, data) const A##n & a##n

#if BOOST_PP_ITERATION() > 0
	template<BOOST_PP_ENUM_PARAMS(BOOST_PP_ITERATION(), class A)>
#endif
	detail::proxy_caller<boost::tuples::tuple<BOOST_PP_ENUM(BOOST_PP_ITERATION(), LUABIND_TUPLE_PARAMS, _)> >
	operator()(BOOST_PP_ENUM(BOOST_PP_ITERATION(), LUABIND_OPERATOR_PARAMS, _)) const
	{
		typedef boost::tuples::tuple<BOOST_PP_ENUM(BOOST_PP_ITERATION(), LUABIND_TUPLE_PARAMS, _)> tuple_t;
#if BOOST_PP_ITERATION() == 0
		tuple_t args;
#else
		tuple_t args(BOOST_PP_ENUM_PARAMS(BOOST_PP_ITERATION(), &a));
#endif
		return detail::proxy_caller<tuple_t>(const_cast<luabind::object*>(this), args);
	}

#undef LUABIND_OPERATOR_PARAMS
#undef LUABIND_TUPLE_PARAMS

#endif