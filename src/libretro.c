#include <libretro.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <gwlua.h>

/*---------------------------------------------------------------------------*/

static void dummy_log( enum retro_log_level level, const char* fmt, ... )
{
  (void)level;
  (void)fmt;
}

retro_log_printf_t log_cb = dummy_log;
retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static struct retro_perf_callback perf_cb;

static gwrom_t rom;
static gwlua_t state;

static struct retro_input_descriptor input_descriptors[] =
{
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L1" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R1" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
  { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
  // TODO: Is this needed?
  { 255, 255, 255, 255, "" }
};

#ifdef LOG_PERFORMANCE
#define RETRO_PERFORMANCE_INIT(name)  static struct retro_perf_counter name = {#name}; if (!name.registered) perf_cb.perf_register(&(name))
#define RETRO_PERFORMANCE_START(name) perf_cb.perf_start(&(name))
#define RETRO_PERFORMANCE_STOP(name)  perf_cb.perf_stop(&(name))
#else
#define RETRO_PERFORMANCE_INIT(name)
#define RETRO_PERFORMANCE_START(name)
#define RETRO_PERFORMANCE_STOP(name)
#endif

/*---------------------------------------------------------------------------*/
/* gwlua user-defined functions */

void* gwlua_malloc( size_t size )
{
  return malloc( size );
}

void gwlua_free( void* pointer )
{
  free( pointer );
}

void* gwlua_realloc( void* pointer, size_t size )
{
  return realloc( pointer, size );
}

void gwlua_play_sound( const gwlua_sound_t* sound, int repeat )
{
  sound->state->position = 0;
  sound->state->playing = sound;
  sound->state->repeat = repeat;
}

void gwlua_stop_all_sounds( gwlua_t* state )
{
  state->playing = NULL;
}

const char* gwlua_load_value( gwlua_t* state, const char* key, int* type )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p, \"%s\", %p )\n", __FUNCTION__, state, key, type );
  return NULL;
}

void gwlua_save_value( gwlua_t* state, const char* key, const char* value, int type )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p, \"%s\", \"%s\", %d )\n", __FUNCTION__, state, key, value, type );
}

int gwlua_set_fb( unsigned width, unsigned height )
{
  struct retro_game_geometry geometry;
  
  geometry.base_width = width;
  geometry.base_height = height;
  geometry.max_width = width;
  geometry.max_height = height;
  geometry.aspect_ratio = 0.0f;
  
  env_cb( RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry );
  
  log_cb( RETRO_LOG_INFO, "gwmw resolution changed to:\n");
  log_cb( RETRO_LOG_INFO, "  width  = %u\n", width );
  log_cb( RETRO_LOG_INFO, "  height = %u\n", height );
  log_cb( RETRO_LOG_INFO, "  pitch  = %u\n", width );
  
  return 0;
}

void gwlua_vlog( const char* format, va_list args )
{
  char buffer[ 8192 ]; /* should be enough */
  
  vsnprintf( buffer, sizeof( buffer ), format, args );
  buffer[ sizeof( buffer ) - 1 ] = 0;
  log_cb( RETRO_LOG_ERROR, "%s", buffer );
}

/*---------------------------------------------------------------------------*/
/* gwrom user-defined functions */

void* gwrom_malloc( size_t size )
{
  return malloc( size );
}

void gwrom_free( void* ptr )
{
  free( ptr );
}

/*---------------------------------------------------------------------------*/

void retro_get_system_info( struct retro_system_info* info )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, info );
  
  info->library_name = "Game & Watch";
  info->library_version = "1.0";
  info->need_fullpath = false;
  info->block_extract = false;
  info->valid_extensions = "mgw";
}

void retro_set_environment( retro_environment_t cb )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, cb );
  
  env_cb = cb;
  
  static const struct retro_variable vars[] = {
    { NULL, NULL },
  };
  
  static const struct retro_controller_description controllers[] = {
    { "Controller", RETRO_DEVICE_JOYPAD },
    // TODO: Is this needed?
    { NULL, 0 }
  };
  
  static const struct retro_controller_info ports[] = {
    { controllers, 1 },
    { NULL, 0 }
  };
  
  cb( RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars );
  cb( RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports );
}

unsigned retro_api_version()
{
  log_cb( RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__ );
  return RETRO_API_VERSION;
}

void retro_init()
{
  log_cb( RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__ );
  
  struct retro_log_callback log;
  
  if ( env_cb( RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log ) )
  {
    log_cb = log.log;
  }
  
  // Always get the perf interface because we need it for the timers
  if ( !env_cb( RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb ) )
  {
    perf_cb.get_time_usec = NULL;
    log_cb( RETRO_LOG_WARN, "Could not get the perf interface\n" );
  }
}

void* constcast( const void* ptr );

bool retro_load_game( const struct retro_game_info* info )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, info );
  
  if ( !perf_cb.get_time_usec )
  {
    log_cb( RETRO_LOG_ERROR, "Core needs the perf interface\n" );
    return false;
  }
  
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
  
  if ( !env_cb( RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt ) )
  {
    log_cb( RETRO_LOG_ERROR, "RGB565 is not supported\n" );
    return false;
  }
  
  env_cb( RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_descriptors );
  
  int res = gwrom_init( &rom, constcast( info->data ), info->size, GWROM_COPY_ALWAYS );
  
  if ( res != GWROM_OK )
  {
    log_cb( RETRO_LOG_ERROR, "Error initializing the rom: ", gwrom_error_message( res ) );
    return false;
  }
  
  if ( gwlua_create( &state, &rom, perf_cb.get_time_usec() ) )
  {
    log_cb( RETRO_LOG_ERROR, "Error inializing gwlua" );
    return false;
  }
  
  return true;
}

size_t retro_get_memory_size( unsigned id )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %u )\n", __FUNCTION__, id );
  return 0;
}

void* retro_get_memory_data( unsigned id )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %u )\n", __FUNCTION__, id );
  return NULL;
}

void retro_set_video_refresh( retro_video_refresh_t cb )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, cb );
  video_cb = cb;
}

void retro_set_audio_sample( retro_audio_sample_t cb )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, cb );
}

void retro_set_audio_sample_batch( retro_audio_sample_batch_t cb )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, cb );
  audio_cb = cb;
}

void retro_set_input_state( retro_input_state_t cb )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, cb );
  input_state_cb = cb;
}

void retro_set_input_poll( retro_input_poll_t cb )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, cb );
  input_poll_cb = cb;
}

void retro_get_system_av_info( struct retro_system_av_info* info )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p )\n", __FUNCTION__, info );
  
  info->geometry.base_width = state.width;
  info->geometry.base_height = state.height;
  info->geometry.max_width = state.width;
  info->geometry.max_height = state.height;
  info->geometry.aspect_ratio = 0.0f;
  info->timing.fps = 60.0;
  info->timing.sample_rate = 44100.0;
  
  log_cb( RETRO_LOG_INFO, "Set retro_system_av_info to:\n" );
  log_cb( RETRO_LOG_INFO, "  base_width   = %u\n", info->geometry.base_width );
  log_cb( RETRO_LOG_INFO, "  base_height  = %u\n", info->geometry.base_height );
  log_cb( RETRO_LOG_INFO, "  max_width    = %u\n", info->geometry.max_width );
  log_cb( RETRO_LOG_INFO, "  max_height   = %u\n", info->geometry.max_height );
  log_cb( RETRO_LOG_INFO, "  aspect_ratio = %f\n", info->geometry.aspect_ratio );
  log_cb( RETRO_LOG_INFO, "  fps          = %f\n", info->timing.fps );
  log_cb( RETRO_LOG_INFO, "  sample_rate  = %f\n", info->timing.sample_rate );
}

void retro_run()
{
  //log_cb( RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__ );
  
  static const struct { unsigned retro; int gw; } map[] =
  {
    { RETRO_DEVICE_ID_JOYPAD_UP,     GWLUA_UP },
    { RETRO_DEVICE_ID_JOYPAD_DOWN,   GWLUA_DOWN },
    { RETRO_DEVICE_ID_JOYPAD_LEFT,   GWLUA_LEFT },
    { RETRO_DEVICE_ID_JOYPAD_RIGHT,  GWLUA_RIGHT },
    { RETRO_DEVICE_ID_JOYPAD_A,      GWLUA_A },
    { RETRO_DEVICE_ID_JOYPAD_B,      GWLUA_B },
    { RETRO_DEVICE_ID_JOYPAD_X,      GWLUA_X },
    { RETRO_DEVICE_ID_JOYPAD_Y,      GWLUA_Y },
    { RETRO_DEVICE_ID_JOYPAD_L,      GWLUA_L1 },
    { RETRO_DEVICE_ID_JOYPAD_R,      GWLUA_R1 },
    { RETRO_DEVICE_ID_JOYPAD_L2,     GWLUA_L2 },
    { RETRO_DEVICE_ID_JOYPAD_R2,     GWLUA_R2 },
    { RETRO_DEVICE_ID_JOYPAD_L3,     GWLUA_L3 },
    { RETRO_DEVICE_ID_JOYPAD_R3,     GWLUA_R3 },
    { RETRO_DEVICE_ID_JOYPAD_SELECT, GWLUA_SELECT },
    { RETRO_DEVICE_ID_JOYPAD_START,  GWLUA_START },
  };
  
  input_poll_cb();
  
  unsigned id;
  
  for ( id = 0; id < sizeof( map ) / sizeof( map [ 0 ] ); id++ )
  {
    int16_t pressed = input_state_cb( 0, RETRO_DEVICE_JOYPAD, 0, map[ id ].retro );
    gwlua_set_button( &state, map[ id ].gw, pressed != 0 );
  }
  
  gwlua_tick( &state, perf_cb.get_time_usec() );
  video_cb( state.updated ? state.screen : NULL, state.width, state.height, state.width * sizeof( uint16_t ) );
  
  memset( state.sound, 0, sizeof( state.sound ) );

  if ( state.playing )
  {
    int16_t* dest = state.sound;
    size_t size = state.playing->size - state.position;
    const size_t max_size = sizeof( state.sound ) / sizeof( state.sound[ 0 ] ) / 2;
    
again:;
    const int16_t* src = state.playing->pcm16 + state.position;
    
    if ( size > max_size )
    {
      size = max_size;
    }
    
    size_t i;
    
    for ( i = size; i != 0; --i )
    {
      *dest++ = *src;
      *dest++ = *src++;
    }
    
    state.position += size;
    
    if ( state.position == state.playing->size )
    {
      if ( state.repeat )
      {
        state.position = 0;
        
        if ( size < max_size )
        {
          size = max_size - size;
          goto again;
        }
      }
      else
      {
        state.playing = NULL;
      }
    }
  }
  
  audio_cb( state.sound, sizeof( state.sound ) / sizeof( state.sound[ 0 ] ) / 2 );
}

void retro_deinit()
{
#ifdef LOG_PERFORMANCE
  perf_cb.perf_log();
#endif
}

void retro_set_controller_port_device( unsigned port, unsigned device )
{
  (void)port;
  (void)device;
  log_cb( RETRO_LOG_DEBUG, "%s( %u, %u )\n", __FUNCTION__, port, device );
}

void retro_reset()
{
  gwlua_reset( &state );
}

size_t retro_serialize_size()
{
  log_cb( RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__ );
  return 0;
}

bool retro_serialize( void* data, size_t size )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p, %u )\n", __FUNCTION__, data, size );
  return false;
}

bool retro_unserialize( const void* data, size_t size )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %p, %u )\n", __FUNCTION__, data, size );
  return false;
}

void retro_cheat_reset()
{
  log_cb( RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__ );
}

void retro_cheat_set( unsigned a, bool b, const char* c )
{
  log_cb( RETRO_LOG_DEBUG, "%s( %u, %s, \"%s\" )\n", __FUNCTION__, a, b ? "true" : "false", c );
}

bool retro_load_game_special(unsigned a, const struct retro_game_info* b, size_t c)
{
  log_cb( RETRO_LOG_DEBUG, "%s( %u, %p, %u )\n", __FUNCTION__, a, b, c );
  return false;
}

void retro_unload_game()
{
  log_cb( RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__ );
  gwlua_destroy( &state );
  gwrom_destroy( &rom );
}

unsigned retro_get_region()
{
  log_cb( RETRO_LOG_DEBUG, "%s()\n", __FUNCTION__ );
  return RETRO_REGION_NTSC;
}
