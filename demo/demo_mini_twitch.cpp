#include <cassert>
#include <cstdio>
#include "mini_twitch.h"

// Create a file 'twitch_config' with the next 3 commented lines, or remove the include and fill in
#include "twitch_config.h"
//const char* TWITCH_CLIENT_ID = "<your_client_id>";
//const char* TWITCH_SECRET    = "<your_secret>";
//const char* TWITCH_GAME_NAME = "<your game if any>;

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace MiniTwitch;

std::string readFile( const char* infilename ) {
  if( FILE* f = fopen( infilename, "rb" )) {
    char buf[1024];
    auto nchars = fread( buf, 1, 1024, f );
    buf[ nchars ] = 0;
    fclose(f);
    return std::string( buf );
  }
  return "";
}

void writeFile( const char* outfilename, const std::string& str ) {
  if( FILE* f = fopen( outfilename, "wb" )) {
    fwrite( str.c_str(), 1, str.size(), f );
    fclose(f);
  }
}

void from_json(const json& j, MiniTwitch::TokenData& p) {
  j.at("token_type").get_to(p.token_type);
  j.at("access_token").get_to(p.access_token);
  j.at("refresh_token").get_to(p.refresh_token);
  j.at("expires_in").get_to(p.expires_in);
  j.at("scope").get_to(p.scope);
}

void to_json(json& j, const MiniTwitch::TokenData& p) {
  j = json{
      { "token_type", p.token_type },
      { "access_token", p.access_token },
      { "refresh_token", p.refresh_token },
      { "expires_in", p.expires_in },
      { "scope", p.scope },
  };
}

void writeToken( const MiniTwitch::TokenData& token ) {
  json j;
  to_json( j, token );
  std::string str = j.dump();
  writeFile( "./demo/token.json", str);
}

bool readToken( TokenData& token ) {
  std::string s = readFile( "./demo/token.json");
  json j = json::parse(s.c_str(), nullptr, false);
  if( j.is_discarded() ) {
    printf( "Json parse failed\n");
    return false;
  }
  from_json( j, token );
  return true;
}

int main(int argc, char** argv)
{
  globalInit();

  AuthProcess proc;
  
  Config& config = proc.config;
  config.client_id = TWITCH_CLIENT_ID;
  config.secret = TWITCH_SECRET;
  config.callback_auth_url = "/auth";
  config.scopes.push_back( "user:read:email" );
  config.scopes.push_back( "channel:read:polls" );
  config.scopes.push_back( "channel:manage:polls" );

  readToken( proc.token );
  bool is_ok = proc.start();
  if( !is_ok )
     return -1;
  writeToken( proc.token );
  
  // At this point we are authorized
  printf( "User Token is %s\n", proc.token.access_token.c_str());
  proc.getUserInfo();
  std::string game_id = proc.getGame(TWITCH_GAME_NAME);
  printf( "game_id is %s\n", game_id.c_str());

  globalCleanup();
  return 0;
}
