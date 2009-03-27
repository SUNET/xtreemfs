#include "policy_container.h"
using namespace org::xtreemfs::client;

#include "org/xtreemfs/interfaces/exceptions.h"

#ifdef _WIN32
#include "yield/platform/windows.h"
#include <lm.h>
#pragma comment( lib, "Netapi32.lib" )
#else
#include "yieldfs.h"
#include <unistd.h>
#include <pwd.h>
#define PWD_BUF_LEN 128
#include <grp.h>
#define GRP_BUF_LEN 1024
#endif


namespace org
{
  namespace xtreemfs
  {
    namespace client
    {
      class PolicyContainerreaddirCallback : public YIELD::Volume::readdirCallback
      {
      public:
        PolicyContainerreaddirCallback( PolicyContainer& policy_container, const YIELD::Path& root_dir_path )
          : policy_container( policy_container ), root_dir_path( root_dir_path )
        { }

        // YIELD::Volume::readdirCallback
        bool operator()( const YIELD::Path& name, const YIELD::Stat& stbuf )
        {
          std::string::size_type dll_pos = name.getHostCharsetPath().find( SHLIBSUFFIX );
          if ( dll_pos != std::string::npos && dll_pos != 0 && name.getHostCharsetPath()[dll_pos-1] == '.' )
            policy_container.loadPolicySharedLibrary( root_dir_path + name );
          return true;
        }

      private:
        PolicyContainer& policy_container;
        YIELD::Path root_dir_path;
      };
    };
  };
};


PolicyContainer::PolicyContainer()
{
  this->get_user_credentials_from_passwd = NULL;
  this->get_passwd_from_user_credentials = NULL;

  loadPolicySharedLibraries( "policies" );
  loadPolicySharedLibraries( "lib" );
  loadPolicySharedLibraries( YIELD::Path() );
}

PolicyContainer::~PolicyContainer()
{
  for ( std::vector<YIELD::SharedLibrary*>::iterator policy_shared_library_i = policy_shared_libraries.begin(); policy_shared_library_i != policy_shared_libraries.end(); policy_shared_library_i++ )
    delete *policy_shared_library_i;
}

void PolicyContainer::loadPolicySharedLibraries( const YIELD::Path& policy_shared_libraries_dir_path )
{
  PolicyContainerreaddirCallback readdir_callback( *this, policy_shared_libraries_dir_path );
  YIELD::Volume().readdir( policy_shared_libraries_dir_path, readdir_callback );
}

void PolicyContainer::loadPolicySharedLibrary( const YIELD::Path& policy_shared_library_file_path )
{
  YIELD::SharedLibrary* policy_shared_library = YIELD::SharedLibrary::open( policy_shared_library_file_path );
  if ( policy_shared_library )
  {
    get_passwd_from_user_credentials_t get_passwd_from_user_credentials = ( get_passwd_from_user_credentials_t )policy_shared_library->getFunction( "get_passwd_from_user_credentials" );
    if ( get_passwd_from_user_credentials )
      this->get_passwd_from_user_credentials = get_passwd_from_user_credentials;

    get_user_credentials_from_passwd_t get_user_credentials_from_passwd = ( get_user_credentials_from_passwd_t )policy_shared_library->getFunction( "get_user_credentials_from_passwd" );
    if ( get_user_credentials_from_passwd )
      this->get_user_credentials_from_passwd = get_user_credentials_from_passwd;

    policy_shared_libraries.push_back( policy_shared_library );
  }
}

void PolicyContainer::getCurrentUserCredentials( org::xtreemfs::interfaces::UserCredentials& out_user_credentials ) const
{
#ifdef _WIN32
  if ( get_user_credentials_from_passwd )
    getUserCredentialsFrompasswd( -1, -1, out_user_credentials );
  else
  {
    DWORD dwLevel = 1;
    LPWKSTA_USER_INFO_1 user_info = NULL;
    if ( NetWkstaUserGetInfo( NULL, dwLevel, (LPBYTE *)&user_info ) == NERR_Success )
    {
      if ( user_info !=NULL )
      {
        size_t username_wcslen = wcslen( user_info->wkui1_username );   
        size_t username_strlen = WideCharToMultiByte( GetACP(), 0, user_info->wkui1_username, username_wcslen, NULL, 0, 0, NULL );
        char* user_id = new char[username_strlen+1];
        WideCharToMultiByte( GetACP(), 0, user_info->wkui1_username, username_wcslen, user_id, username_strlen+1, 0, NULL );
        out_user_credentials.set_user_id( user_id, username_strlen );
        delete [] user_id;

        size_t logon_domain_wcslen = wcslen( user_info->wkui1_logon_domain );
        size_t logon_domain_strlen = WideCharToMultiByte( GetACP(), 0, user_info->wkui1_logon_domain, logon_domain_wcslen, NULL, 0, 0, NULL );
        char* group_id = new char[logon_domain_strlen+1];
        WideCharToMultiByte( GetACP(), 0, user_info->wkui1_logon_domain, logon_domain_wcslen, group_id, logon_domain_strlen+1, 0, NULL );
        std::string group_id_str( group_id, logon_domain_strlen );
        delete [] group_id;
        org::xtreemfs::interfaces::StringSet group_ids;
        group_ids.push_back( group_id_str );
        out_user_credentials.set_group_ids( group_ids );

        NetApiBufferFree( user_info );

        return;
      }
    }

    throw YIELD::PlatformException( ERROR_ACCESS_DENIED, "could not retrieve user_id and group_id" );
  }
#else
  int caller_uid = yieldfs::FUSE::geteuid();
  if ( caller_uid < 0 ) caller_uid = ::geteuid();
  int caller_gid = yieldfs::FUSE::getegid();
  if ( caller_gid < 0 ) caller_gid = ::getegid();
  getUserCredentialsFrompasswd( caller_uid, caller_gid, out_user_credentials );
#endif
}

void PolicyContainer::getUserCredentialsFrompasswd( int uid, int gid, org::xtreemfs::interfaces::UserCredentials& out_user_credentials ) const
{
  if ( get_user_credentials_from_passwd )
  {
    size_t user_id_len, group_ids_len;
    int get_user_credentials_from_passwd_ret = get_user_credentials_from_passwd( uid, gid, NULL, &user_id_len, NULL, &group_ids_len );
    if ( get_user_credentials_from_passwd_ret >= 0 )
    {
      if ( user_id_len > 0 && group_ids_len > 0 )
      {
        char* user_id = new char[user_id_len];
        char* group_ids = new char[group_ids_len];
        
        get_user_credentials_from_passwd_ret = get_user_credentials_from_passwd( uid, gid, user_id, &user_id_len, group_ids, &group_ids_len );
        if ( get_user_credentials_from_passwd_ret >= 0 )
        {
          out_user_credentials.set_user_id( user_id );

          char* group_ids_p = group_ids;
          org::xtreemfs::interfaces::StringSet group_ids_ss;
          while ( static_cast<size_t>( group_ids_p - group_ids ) < group_ids_len )
          {
            group_ids_ss.push_back( group_ids_p );
            group_ids_p += group_ids_ss.back().size() + 1;
          }
          out_user_credentials.set_group_ids( group_ids_ss );
        }
        else
          throw YIELD::PlatformException( get_user_credentials_from_passwd_ret * -1 );
      }
    }
    else
      throw YIELD::PlatformException( get_user_credentials_from_passwd_ret * -1 );
  }

#ifdef _WIN32
  YIELD::DebugBreak();
#else
    struct passwd pwd, *pwd_res;
    char pwd_buf[PWD_BUF_LEN]; int pwd_buf_len = sizeof( pwd_buf );
    struct group grp, *grp_res;
    char grp_buf[GRP_BUF_LEN]; int grp_buf_len = sizeof( grp_buf );

    if ( getpwuid_r( caller_uid, &pwd, pwd_buf, pwd_buf_len, &pwd_res ) == 0 && pwd_res != NULL && pwd_res->pw_name != NULL &&
         getgrgid_r( caller_gid, &grp, grp_buf, grp_buf_len, &grp_res ) == 0 && grp_res != NULL && grp_res->gr_name != NULL )
      return new org::xtreemfs::interfaces::UserCredentials( pwd_res->pw_name, org::xtreemfs::interfaces::StringSet( grp_res->gr_name ), "" );

  throw YIELD::PlatformException( EACCES, "could not retrieve user_id and group_id" );
#endif
}

void PolicyContainer::getpasswdFromUserCredentials( const std::string& user_id, const std::string& group_id, int& out_uid, int& out_gid )
{
  if ( get_passwd_from_user_credentials )
    YIELD::DebugBreak(); // TODO: implement me

#ifdef _WIN32
  YIELD::DebugBreak();
#else
  if ( !user_id.empty() && !group_id.empty() )
  {
    struct passwd pwd, *pwd_res;
    char pwd_buf[PWD_BUF_LEN]; int pwd_buf_len = sizeof( pwd_buf );
    struct group grp, *grp_res;
    char grp_buf[GRP_BUF_LEN]; int grp_buf_len = sizeof( grp_buf );

    if ( getpwnam_r( user_id.c_str(), &pwd, pwd_buf, pwd_buf_len, &pwd_res ) == 0 && pwd_res != NULL 
         getgrnam_r( group_id.c_str(), &grp, grp_buf, grp_buf_len, &grp_res ) == 0 && grp_res != NULL )
    {
      out_uid = pwd_res->pw_uid;
      out_gid = grp_res->gr_gid;
      return;
    }
  }

  throw YIELD::PlatformException( EACCES, "could not retrieve uid and gid" );
#endif
}

