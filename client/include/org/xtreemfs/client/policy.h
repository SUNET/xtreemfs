#ifndef ORG_XTREEMFS_CLIENT_POLICY_H
#define ORG_XTREEMFS_CLIENT_POLICY_H

#ifndef DLLEXPORT
#if defined(_MSC_VER)
#define DLLEXPORT extern "C" __declspec(dllexport)
#elif  defined(__GNUC__) && __GNUC__ >= 4
#define DLLEXPORT extern "C" __attribute__ ( ( visibility( "default" ) ) )
#else
#define DLLEXPORT extern "C"
#endif
#endif


typedef int ( *get_user_credentials_from_passwd_t )( int caller_uid, int caller_gid, char* out_user_id, size_t* out_user_id_size, char* out_group_ids, size_t* out_group_ids_size );
typedef int ( *get_passwd_from_user_credentials_t )( const char* user_id, const char* group_ids, int* uid, int* gid );
                                             
#endif
