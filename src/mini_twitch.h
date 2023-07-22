#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace MiniTwitch
{

  // -------------------------------------------------------
  using VScopes = std::vector< std::string >;

  struct Config {
    std::string client_id;
    std::string secret;
    std::string callback_auth_url;
    int         port = 3003;
    VScopes     scopes;
  };

  struct TokenData {
    std::string token_type;     // Bearer
    std::string access_token;
    std::string refresh_token;
    int         expires_in = 0;
    VScopes     scope;
  };

  struct AuthProcess {
    Config      config;

    enum class eAuthState {
      NotStarted,
      Authorizing,
      CompletedError,
      CompletedOK
    };
    eAuthState  auth_state = eAuthState::Authorizing;

    //Answer
    std::string scopes_str;
    std::string redirect_uri;
    std::string full_answer;
    TokenData   token;

    // Random data to confirm the server is answering to our request
    std::string state;

    std::string recv_state;
    std::string recv_scope;
    std::string recv_code;

    bool start();
    json getUserInfo() const;
    std::string getGame( const std::string& game_name ) const;
    bool authGet( const std::string& url, json& jout ) const;

  private:
    bool validate();
    bool generateNewToken();
  };

}