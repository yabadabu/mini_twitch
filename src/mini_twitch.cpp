#include <cstdio>
#include <cstdarg>
#include <ctime>
#include "mini_twitch.h"
#include "http_server.h"

extern "C" {
#include <curl/curl.h>
}

namespace MiniTwitch {

  static const char* url_twitch_authorize = "https://id.twitch.tv/oauth2/authorize";
  static const char* url_twitch_auth_token = "https://id.twitch.tv/oauth2/token";
  static const std::string url_twitch_validate_token = "https://id.twitch.tv/oauth2/validate";
  static const std::string url_twitch_users = "https://api.twitch.tv/helix/users";
  static const std::string url_twitch_game = "https://api.twitch.tv/helix/games?name=";
  static const char* content_type_urlencoded = "Content-Type: application/x-www-form-urlencoded";
  static const char* accept_twitch_tv_json = "Accept: application/vnd.twitchtv.v5+json";

  void from_json(const json& j, MiniTwitch::TokenData& p) {
    j.at("token_type").get_to(p.token_type);
    j.at("access_token").get_to(p.access_token);
    j.at("refresh_token").get_to(p.refresh_token);
    j.at("expires_in").get_to(p.expires_in);
    j.at("scope").get_to(p.scope);
  }

  // --------
  struct ValidationAnswer {
    std::string client_id;
    std::string login;
    std::string user_id;
    int         expires_in = 0;
    VScopes     scopes;
  };
  void from_json(const json& j, ValidationAnswer& p) {
    j.at("client_id").get_to(p.client_id);
    j.at("login").get_to(p.login);
    j.at("user_id").get_to(p.user_id);
    j.at("expires_in").get_to(p.expires_in);
    j.at("scopes").get_to(p.scopes);
  }

  static size_t storeAnswerCallback(void *contents, size_t size, size_t nmemb, void *userp) {
      ((std::string*)userp)->append((char*)contents, size * nmemb);
      return size * nmemb;
  }

  bool requestToken(AuthProcess* p) {

      if( p->recv_state != p->state ) {
        printf( "States do not match %s vs %s\n", p->state.c_str(), p->recv_state.c_str() );
        p->auth_state = AuthProcess::eAuthState::CompletedError;
        return false;
      }

      std::string content = "client_id=" + p->config.client_id
                          + "&client_secret=" + p->config.secret
                          + "&code=" + p->recv_code
                          + "&grant_type=authorization_code"
                          + "&redirect_uri=" + p->redirect_uri
                          ;

      CURL* curl = curl_easy_init();
      curl_easy_setopt(curl, CURLOPT_URL, url_twitch_auth_token);
      curl_easy_setopt(curl, CURLOPT_POST, 1);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, content.c_str());
      //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
      curl_slist* chunk = curl_slist_append(nullptr, content_type_urlencoded);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

      std::string answer;
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &storeAnswerCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer);
      CURLcode res = curl_easy_perform(curl);
      if (res == CURLE_OK) {
        printf( "Answer is %s\n", answer.c_str());
        p->full_answer = answer;

        json j = json::parse(answer.c_str(), nullptr, false);
        if (!j.is_discarded()) {
          p->token = j.get<TokenData>();
          p->auth_state = AuthProcess::eAuthState::CompletedOK;
        }

      } else {
        printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        p->auth_state = AuthProcess::eAuthState::CompletedError;
      }
      curl_easy_cleanup(curl);
      curl_slist_free_all(chunk);
      return (res == CURLE_OK);
  }

  class AuthServer : public HTTP::CBaseServer {
  public:
    AuthProcess* proc = nullptr;
    bool onClientRequest(const TRequest& r) override {
      if( r.getURLPath() == proc->config.callback_auth_url ) {
        proc->recv_code = r.getURIParam( "code" );
        proc->recv_scope = r.getURIParam( "scope" );
        proc->recv_state = r.getURIParam( "state" );
        requestToken(proc);
      } else {
        printf( "URL %s is not valid!\n", r.getURLPath().c_str());
        proc->auth_state = AuthProcess::eAuthState::CompletedError;
      }

      // Send a empty answer to the browser as part of the redirect
      HTTP::VBytes ans;
      sendAnswer( r, ans, "text/html", nullptr );
      return false;
    }
  };

  std::string replaceAll(const std::string& in_str, const std::string& match, const std::string& new_str) {
    std::string out_str = in_str;
    size_t start_pos = 0;
    while((start_pos = out_str.find(match, start_pos)) != std::string::npos) {
        out_str.replace(start_pos, match.length(), new_str);
        start_pos += new_str.length();
    }
    return out_str;
  }

  std::string urlEncode( const std::string& s ) {
    std::string out = replaceAll( s, " ", "%20");
    return out;
  }

  static std::string join( const std::vector< std::string >& items, const std::string& join_str ) {
    std::string r;
    for( size_t i=0; i<items.size(); ++i ) {
      if( i != 0 )
        r += join_str;
      r.append( items[i] );
    }
    return r;
  }

  bool AuthProcess::generateNewToken() {
    if( config.client_id.empty() || config.secret.empty() || config.callback_auth_url.empty() )
      return false;

    // Convert scopes as encoded line
    scopes_str = join( config.scopes, "+");
    scopes_str = replaceAll( scopes_str, ":", "%3A");

    // Generate some random id so this request is unique
    state.clear();
    srand( time(nullptr));
    for( int i=0; i<16; ++i ) {
      int v = rand();
      char buf[8];
      sprintf( buf, "%02x", v & 0xff );
      state.append( buf );
    }
    //printf( "State will be %s\n", state.c_str());
    redirect_uri = "http://localhost:" + std::to_string(config.port) + config.callback_auth_url;

    std::string auth_url = url_twitch_authorize + std::string("?response_type=code")
      + "&client_id=" + config.client_id
      + "&redirect_uri=" + redirect_uri
      + "&scope=" + scopes_str
      + "&state=" + state;

    AuthServer server;
    server.proc = this;
    server.trace = true;

    //server.trace = true;
    if (!server.open(config.port)) {
      printf( "Can't start server at port %d\n", config.port);
      return false;
    }
    printf( "Open.URL\n%s\n", auth_url.c_str());

    auth_state = eAuthState::Authorizing;
    while (auth_state == eAuthState::Authorizing) {
      if (!server.tick(1000000)) {
        printf( ".");
      }
    }

    return auth_state == eAuthState::CompletedOK;
  }

  bool AuthProcess::authGet( const std::string& url, json& jout ) const {

    if( token.access_token.empty() ) {
      printf( "authGet failed. Token is empty.\n");
      return false;
    }

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    std::string header_client_id = "Client-ID:" + config.client_id;
    std::string header_auth_bearer = "Authorization: Bearer " + token.access_token;

    curl_slist* chunk = curl_slist_append(nullptr, header_client_id.c_str());
    chunk = curl_slist_append(chunk, accept_twitch_tv_json);
    chunk = curl_slist_append(chunk, header_auth_bearer.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

    std::string answer;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &storeAnswerCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer);
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    CURLcode res = curl_easy_perform(curl);
    bool is_ok = (res == CURLE_OK);
    if (is_ok) {
      //printf( "Answer is %s\n", answer.c_str());
      jout = json::parse(answer.c_str(), nullptr, false);
      if (jout.is_discarded()) {
        printf( "Invalid json as answer: %s\n", answer.c_str());
        is_ok = false;
      } else {
        //jout = j.at("data");
      }
    } else {
      printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);
    return is_ok;
  }

  bool AuthProcess::validate() {
    json j;
    if( authGet( url_twitch_validate_token, j ) ) {
      ValidationAnswer ans = j.get<ValidationAnswer>();
      if( ans.client_id == config.client_id ) {
        auth_state = AuthProcess::eAuthState::CompletedOK;
        token.expires_in = ans.expires_in;
        return true;
      } else {
        printf( "client_id of token does not match the config\n");
      }
    }
    return false;
  }

  json AuthProcess::getUserInfo() const {
    json j;
    if( authGet( url_twitch_users, j ) ) {
      printf( "User: %s\n", j.at("data")[0].dump().c_str());
    }
    return j;
  }

  std::string AuthProcess::getGame(const std::string& game_name) const {
    std::string game_id;
    std::string url = url_twitch_game + urlEncode( game_name );
    json j;
    if( authGet( url, j )) {
      const json& jd = j.at("data");
      if( jd.size() > 0 ) {
        jd[0].at("id").get_to(game_id);
      }
    }
    return game_id;
  }

  bool AuthProcess::start() {
    if( validate() )
      return true;
    return generateNewToken();
  }

}