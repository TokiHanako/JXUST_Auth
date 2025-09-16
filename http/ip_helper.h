#pragma once
#include <string>
#include <list>
#include <memory>

#if defined(_WIN32) || defined(_WIN64)
	#ifndef os_is_win	
		#define os_is_win
	#endif // !os_is_win

#elif defined(__linux__) || defined(_linux_) || defined(__unix) || defined(_unix_) || defined(_APPLE_)
	#ifndef os_is_linux	
		#define os_is_linux
	#endif // !os_is_linux
	
#endif// ! defined(_WIN32) || defined(_WIN64)




#if defined(os_is_win)

	#ifndef	_net_api_export_
		#define _net_api_export_ __declspec(dllexport)
	#else 
		#define _net_api_export_ __declspec(dllimport)
	#endif // !#ifndef	_net_api_export_

#elif defined(os_is_linux)

	#ifndef	_net_api_export_
		#define _net_api_export_  __attribute__((visibility ("default")))
	#endif // !_net_api_export_

#endif //! defined(os_is_win)



namespace lib_net
{


	/**
	* @brief: ip information
	*/
	struct ip_info_
	{
		std::string _inet4;
		std::string _inet6;
		std::string _subnet_mask;
		std::string _gate;

		void zero()
		{
			_inet4			= { "" };
			_inet6			= { "" };
			_subnet_mask	= { "" };
			_gate			= { "" };
		}


		ip_info_()
		{
			zero();
		}
	};

	using ip_info		= ip_info_;
	using ip_list		= std::list< ip_info>;


	/**
	* @brief: the information of adapter
	*/
	struct net_adapter_info_
	{
#ifdef os_is_win
		int			_index;
#endif //! os_is_win

		std::string _name;
		std::string _description;
		std::string _dev_type;
		std::string _mac;

		ip_list		_ip;
		int			_ip_size;

		void zero()
		{
#ifdef os_is_win
			_index				= 0;
#endif //! os_is_win
			_name				= { "" };
			_description		= { "" };
			_dev_type			= { "" };
			_mac				= { "" };
			_ip.clear();
			_ip_size			= 0;
		}

		net_adapter_info_()
		{
			zero();
		}
	};

	// to rename the type 
	using net_ada_info = net_adapter_info_;

	// maybe, this machine has greater than 1 adapter
	using net_ada_list = std::list<net_ada_info>;

//----------------------------------------------------------------------------------------




	/**
	* @brief: you could get the adapter information through this class on windows, linux and osx
	*/
	class _net_api_export_ net_adapter_helper
	{
	public:
	//----------------------------------------------------------------------------------------
		
		static net_adapter_helper& get_instance();

		/**
		* @brief: 获取Windows网卡信息
		*/
		net_ada_list get_info_win();




	private:
		template<typename ... Args>
		static std::string str_format(const std::string &format, Args ... args)
		{
			auto size_buf = std::snprintf(nullptr, 0, format.c_str(), args ...) + 1;
			std::unique_ptr<char[]> buf(new(std::nothrow) char[size_buf]);

			if (!buf)
				return std::string("");

			std::snprintf(buf.get(), size_buf, format.c_str(), args ...);

			return std::string(buf.get(), buf.get() + size_buf - 1);
		}

	//----------------------------------------------------------------------------------------
		net_adapter_helper() = default;
		virtual ~net_adapter_helper() = default;

		net_adapter_helper(const net_adapter_helper& instance) = delete;
		net_adapter_helper& operator = (const net_adapter_helper& instance) = delete;
		net_adapter_helper(const net_adapter_helper&& instance) = delete;
		net_adapter_helper& operator = (const net_adapter_helper&& instance) = delete;

	};
}