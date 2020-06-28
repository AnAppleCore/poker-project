#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <getopt.h>
#include <string>
#include <cstring>
#include "game.h"
#include "rng.h"
#include "net.h"
#include <fstream>
using namespace std;

/*
  ./play_match.pl sample holdem.nolimit.2p.reverse_blinds.game 1000 0 my ./my_player example ./example_player
*/

static double WINPROB[13][13] = {
0.496566,    0.303721,    0.308485,    0.319218,    0.322348,    0.327069,    0.347315,    0.382924,    0.401701,    0.430468,    0.460827,    0.491453,    0.536624,
0.306047,    0.527608,    0.333814,    0.341972,    0.339238,    0.343853,    0.353946,    0.377559,    0.416423,    0.438859,    0.467539,    0.502416,    0.543980,
0.312816,    0.332048,    0.567133,    0.354946,    0.351677,    0.364884,    0.375205,    0.387367,    0.416099,    0.449986,    0.481474,    0.515345,    0.555439,
0.316639,    0.336948,    0.358217,    0.590022,    0.381018,    0.382725,    0.392471,    0.415029,    0.432945,    0.456287,    0.484941,    0.514508,    0.555805,
0.319374,    0.343971,    0.363180,    0.379366,    0.624930,    0.402863,    0.423940,    0.433509,    0.447531,    0.462062,    0.496657,    0.537409,    0.568596,
0.329109,    0.349875,    0.370693,    0.381312,    0.409911,    0.651992,    0.430701,    0.448375,    0.465207,    0.476969,    0.504219,    0.538815,    0.578182,
0.352351,    0.353999,    0.373832,    0.395730,    0.413408,    0.436416,    0.683618,    0.467580,    0.487937,    0.496431,    0.528881,    0.550280,    0.583261,
0.373435,    0.389426,    0.388342,    0.415157,    0.418856,    0.455151,    0.464009,    0.719881,    0.504976,    0.527769,    0.543412,    0.572151,    0.600504,
0.400716,    0.412074,    0.409463,    0.424126,    0.446636,    0.464959,    0.486049,    0.498205,    0.742406,    0.545498,    0.570036,    0.591750,    0.623637,
0.424404,    0.437180,    0.447340,    0.452953,    0.465407,    0.490233,    0.505591,    0.524568,    0.544317,    0.767634,    0.573063,    0.604649,    0.629498,
0.454989,    0.468453,    0.475777,    0.479523,    0.496927,    0.504127,    0.536185,    0.551243,    0.559404,    0.576618,    0.798437,    0.599020,    0.635181,
0.495075,    0.500827,    0.514347,    0.527413,    0.536253,    0.539280,    0.549305,    0.569255,    0.588653,    0.602194,    0.610224,    0.817866,    0.645729,
0.535888,    0.534186,    0.551310,    0.558223,    0.559572,    0.576591,    0.594773,    0.594799,    0.613382,    0.633979,    0.635784,    0.647952,    0.851298,
};
static char suitChars[ MAX_SUITS + 1 ] = "cdhs";
static char rankChars[ MAX_RANKS + 1 ] = "23456789TJQKA";

/*find the winning probability for holecards*/
double setProb(int Cardsrank1, int Cardsrank2, bool issamesuit){
  int i=Cardsrank1;
  int j=Cardsrank2;
  double p;
  if(i > j){
    swap(i,j);
  }
  if(issamesuit){
    p = WINPROB[i][j];
  }
  else if(!issamesuit){
    p = WINPROB[j][i];
  }
  return p;
}

Action act(Game *game, MatchState *state, rng_state_t *rng) {
  Action action;
  uint8_t Round;    //current round;
  int card_info[7];
  int HoleCards[4];/*contain the rank and suit for each card*/
  int BoardCards[10];
  double state_info[14];
  double probs[NUM_ACTION_TYPES];
  double actionProbs[NUM_ACTION_TYPES];
  double raise_rate;
  int32_t raise_size;
  /*
    coeffcients to compute probabilities, 4*4 outputs and 9 variables;
    from pre-flop round to river round;
    from call-0, fold-1, raise-2, to raise_rate-3;
    from holecards_winprob-0, 10 board-holecards_win-1:10, two spent value-11\12, to const-13;
  */
  double coefficients[4][4][14];
  string state_line;
  string lasthandid;
  int lasthandresult;// 0: win; 1: lose; 2: draw
  int32_t value_seat_2;
  int32_t abs_value;
  double p;
  int a;

  /*read the essiential game information from state*/
  {
    /*obtain current round*/
    Round = state->state.round;
    /*obtain holecardsand*/
    card_info[0] = state->state.holeCards[currentPlayer(game, &(state->state))][0];
    card_info[1] = state->state.holeCards[currentPlayer(game, &(state->state))][1];
    /*obtain the boradcards*/
    for(int i=0; i<=4; i++){
      card_info[i+2] = state->state.boardCards[i];
    }
    state_info[11] = state->state.spent[state->viewingPlayer];
    state_info[12] = state->state.spent[1-state->viewingPlayer];
    state_info[13] = 1;

    /*get the pair of my holecards*/
    HoleCards[0] = rankOfCard(card_info[0]);
    HoleCards[1] = suitOfCard(card_info[0]);
    HoleCards[2] = rankOfCard(card_info[1]);
    HoleCards[3] = suitOfCard(card_info[1]);
    /*obtain the winning probability between holecards*/
    if(HoleCards[1] == HoleCards[3]){
      state_info[0]=setProb(HoleCards[0], HoleCards[2], 1);
    }
    else  state_info[0]=setProb(HoleCards[0], HoleCards[2], 0);

    /*obtain the boradcards and the winning prob between each pair of holecards and board cards*/
    for(int i=0; i<=4; i++){
      BoardCards[2*i] = rankOfCard(card_info[i+2]);
      BoardCards[2*i+1] = suitOfCard(card_info[i+2]);
    }

    /*obtain the winning probability between holecards and boradcards*/
    switch (Round)
    {
    case 0://pre-flop
      for (int i = 1; i <= 10; i++)
      {
        state_info[i] = 0.0;
      }
      
        break;
    case 1:/*flop*/
      for (int i = 1; i <= 3; i++)
      {
          if(HoleCards[1]==BoardCards[2*i-1]) state_info[i]=setProb(HoleCards[0], BoardCards[2*i-2], 1);
          else state_info[i]=setProb(HoleCards[0], BoardCards[2*i-2], 0);
      }
      for (int i = 1; i <= 3; i++)
      {
          if(HoleCards[3]==BoardCards[2*i-1]) state_info[i+3]=setProb(HoleCards[2], BoardCards[2*i-2], 1);
          else state_info[i+3]=setProb(HoleCards[2], BoardCards[2*i-2], 0);
      }
        break;

    case 2:/*turn*/
      for (int i = 1; i <= 4; i++)
      {
          if(HoleCards[1]==BoardCards[2*i-1]) state_info[i]=setProb(HoleCards[0], BoardCards[2*i-2], 1);
          else state_info[i]=setProb(HoleCards[0], BoardCards[2*i-2], 0);
      }
      for (int i = 1; i <= 4; i++)
      {
          if(HoleCards[3]==BoardCards[2*i-1]) state_info[i+4]=setProb(HoleCards[2], BoardCards[2*i-2], 1);
          else state_info[i+4]=setProb(HoleCards[2], BoardCards[2*i-2], 0);
      }
        break;

    case 3:/*river*/
      for (int i = 1; i <= 5; i++)
      {
          if(HoleCards[1]==BoardCards[2*i-1]) state_info[i]=setProb(HoleCards[0], BoardCards[2*i-2], 1);
          else state_info[i]=setProb(HoleCards[0], BoardCards[2*i-2], 0);
      }
      for (int i = 1; i <= 5; i++)
      {
          if(HoleCards[3]==BoardCards[2*i-1]) state_info[i+5]=setProb(HoleCards[2], BoardCards[2*i-2], 1);
          else state_info[i+5]=setProb(HoleCards[2], BoardCards[2*i-2], 0);
      }
        break;

    default:
        break;
    }
  }

  /*read parameters from record.txt file*/
  ifstream record;
  record.open("./record_B.txt", ios::in);
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      for (int k = 0; k < 14; k++)
      {
        record >> coefficients[i][j][k];
      }
    }
  }
  record.close();

  /*read the result and value of last round*/
  state_line = "";
  ifstream logfile;
  logfile.open("./logs/sample.log", ios::in);
  if(state->state.handId != 0){
    lasthandid = to_string(state->state.handId-1);
    while(state_line.find(lasthandid)!=6){
      logfile >> state_line;
    }
    int position1 = state_line.find('#');
    int position2 = state_line.find_last_of(':');
    int position3 = state_line.find_last_of('|');
    //last hand betting value os the player in the second seat
    value_seat_2 = atoi(state_line.substr(position1+1, position2-position1-1).c_str());
    //last hand absolut value
    abs_value = abs(value_seat_2); 
    if(value_seat_2 > 0){
      if(state_line.at(position3+1) == 'B'){
        lasthandresult = 0;
      }
      else lasthandresult = 1;
    }
    else if(value_seat_2 < 0){
      if(state_line.at(position3+1) == 'B'){
        lasthandresult = 1;
      }
      else lasthandresult = 0;
    }
    else lasthandresult = 2;
  }
  else{// for the first hand, assume the result is draw
    lasthandresult = 2;
    abs_value = value_seat_2 = 0;
  }
  /*try to find wether the result is valid*/
    printf("Hand:%d,\t result:%d,\t value_seat_2:%d,\t abs_value:%d\n", state->state.handId-1, lasthandresult, value_seat_2, abs_value);
  logfile.close();

  /*
    modify the coefficients in the corresponding round
    and define the probabilities of actions for the player
  */
  p = 0.5 * (1.0-(double)state->state.handId / 1000) * (( double )abs_value / 20000.0);
  assert(p<=0.5);
  // p = 0.1 * ( double )abs_value / 20000.0;
  switch (lasthandresult)
  {
  case 0:/*viewingPlayer has won the last hand*/
    break;
  
  case 1:/*lose*/
    p = - p;
    break;

  case 2:/*draw*/
    p = 0.0;
    break;
  
  default:
    break;
  }
  probs[ a_fold ] = 0.0;
  probs[ a_call ] = 0.0;
  probs[ a_raise ] = 0.0;
  raise_rate = 0.0;
  for (int i = 0; i < 11; i++)
  {
    /*call*/
    coefficients[Round][0][i] += 100000 * p;
    probs[ a_call ] += coefficients[Round][0][i] * state_info[i];
    /*fold*/
    coefficients[Round][1][i] -= 100000 * p;
    probs[ a_fold ] += coefficients[Round][1][i] * state_info[i];
    /*raise*/
    coefficients[Round][2][i] += 100000 * p;
    probs[ a_raise ] += coefficients[Round][2][i] * state_info[i];
    /*raise_rate*/
    coefficients[Round][3][i] += 100000 * p;
    raise_rate += coefficients[Round][3][i] * state_info[i];
  }
  for (int i = 11; i < 13; i++)
  {
    /*call*/
    coefficients[Round][0][i] += 10 * p;
    probs[ a_call ] += coefficients[Round][0][i] * state_info[i];
    /*fold*/
    coefficients[Round][1][i] -= 10 * p;
    probs[ a_fold ] += coefficients[Round][1][i] * state_info[i];
    /*raise*/
    coefficients[Round][2][i] += 10 * p;
    probs[ a_raise ] += coefficients[Round][2][i] * state_info[i];
    /*raise_rate*/
    coefficients[Round][3][i] += 10 * p;
    raise_rate += coefficients[Round][3][i] * state_info[i];
  }
  /*call*/
  coefficients[Round][0][13] += 1000 * p;
  probs[ a_call ] += coefficients[Round][0][13] * state_info[13];
  /*fold*/
  coefficients[Round][1][13] -= 1000 * p;
  probs[ a_fold ] += coefficients[Round][1][13] * state_info[13];
  /*raise*/
  coefficients[Round][2][13] += 1000 * p;
  probs[ a_raise ] += coefficients[Round][2][13] * state_info[13];
  /*raise_rate*/
  coefficients[Round][3][13] += 1000 * p;
  raise_rate += coefficients[Round][3][13] * state_info[13];

  if((probs[ a_fold ] < 0 || probs[ a_call ] < 0 || probs[ a_raise ] < 0)||
  (probs[ a_fold ] == 0 && probs[ a_call ] == 0 && probs[ a_raise ] == 0)){
    probs[ a_fold ] = 0.06;
    probs[ a_call ] = ( 1.0 - probs[ a_fold ] ) * 0.5;  //0.47
    probs[ a_raise ] = ( 1.0 - probs[ a_fold ] ) * 0.5; //0.47
  }

  /*build the set of valid actions, initialization*/
  p = 0.0;
  for ( a = 0; a < NUM_ACTION_TYPES; ++a)
  {
    actionProbs[a] = 0.0;
  }

  /* consider fold */
  action.type = a_fold;
  action.size = 0;
  if( isValidAction( game, &(state->state), 0, &action ) ) {
    actionProbs[ a_fold ] = probs[ a_fold ];
    p += probs[ a_fold ];
  }

  /* consider call */
  action.type = a_call;
  action.size = 0;
  actionProbs[ a_call ] = probs[ a_call ];
  p += probs[ a_call ];

  /* consider raise */
  int32_t min, max;
  if( raiseIsValid( game, &(state->state), &min, &max ) ) {
    actionProbs[ a_raise ] = probs[ a_raise ];
    p += probs[ a_raise ];
    raise_size = (int) raise_rate * ( max - min + 1 );
    if(raise_size >= max-min+1){
      raise_size = max - min;
    }
    else if(raise_size <= 0){
      raise_size = 0;
    }
  }

  /* normalise the probabilities  */
  assert( p > 0.0 );
  for( a = 0; a < NUM_ACTION_TYPES; ++a ) {

    actionProbs[ a ] /= p;
  }

  /* choose one of the valid actions at random */
  p = genrand_real2( rng );
  for( a = 0; a < NUM_ACTION_TYPES - 1; ++a ) {
    if( p <= actionProbs[ a ] ) {
      break;
    }
    p -= actionProbs[ a ];
  }
  action.type = (enum ActionType)a;
  if( a == a_raise ) {
    action.size = min + raise_size;
  }
  else {
    action.size = 0;
  }

  /*write parameters into record.txt file*/
  ofstream record_out;
  record_out.open("./record_B.txt", ios::out);
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      for (int k = 0; k < 14; k++)
      {
        record_out << coefficients[i][j][k] << ' ';
      }
      record_out << endl;
    }
  }
  record_out.close();

  return action;
}

/* Anything related with socket is handled below. */
/* If you are not interested with protocol details, you can safely skip these. */

int step(int len, char line[], Game *game, MatchState *state, rng_state_t *rng) {
  /* add a colon (guaranteed to fit because we read a new-line in fgets) */
  line[ len ] = ':';
  ++len;

  Action action = act(game, state, rng);
  // printf("action: %d\n", action.type);

  /* do the action! */
  assert( isValidAction( game, &(state->state), 0, &action ) );
  int r = printAction( game, &action, MAX_LINE_LEN - len - 2, &line[ len ] );
  if( r < 0 ) {
    fprintf( stderr, "ERROR: line too long after printing action\n" );
    exit( EXIT_FAILURE );
  }
  len += r;
  line[ len ] = '\r';
  ++len;
  line[ len ] = '\n';
  ++len;

  return len;
}


int main( int argc, char **argv )
{
  int sock, len;
  uint16_t port;
  Game *game;
  MatchState state;
  FILE *file, *toServer, *fromServer;
  struct timeval tv;
  rng_state_t rng;
  char line[ MAX_LINE_LEN ];

  /* we make some assumptions about the actions - check them here */
  assert( NUM_ACTION_TYPES == 3 );

  if( argc < 3 ) {

    fprintf( stderr, "usage: player server port\n" );
    exit( EXIT_FAILURE );
  }

  /* Initialize the player's random number state using time */
  gettimeofday( &tv, NULL );
  init_genrand( &rng, tv.tv_usec );

  /* get the game */
  file = fopen( "./holdem.nolimit.2p.reverse_blinds.game", "r" );
  if( file == NULL ) {

    fprintf( stderr, "ERROR: could not open game './holdem.nolimit.2p.reverse_blind.game'\n");
    exit( EXIT_FAILURE );
  }
  game = readGame( file );
  if( game == NULL ) {

    fprintf( stderr, "ERROR: could not read game './holdem.nolimit.2p.reverse_blind.game'\n");
    exit( EXIT_FAILURE );
  }
  fclose( file );

  /* connect to the dealer */
  if( sscanf( argv[ 2 ], "%"SCNu16, &port ) < 1 ) {

    fprintf( stderr, "ERROR: invalid port %s\n", argv[ 2 ] );
    exit( EXIT_FAILURE );
  }
  sock = connectTo( argv[ 1 ], port );
  if( sock < 0 ) {

    exit( EXIT_FAILURE );
  }
  toServer = fdopen( sock, "w" );
  fromServer = fdopen( sock, "r" );
  if( toServer == NULL || fromServer == NULL ) {

    fprintf( stderr, "ERROR: could not get socket streams\n" );
    exit( EXIT_FAILURE );
  }

  /* send version string to dealer */
  if( fprintf( toServer, "VERSION:%"PRIu32".%"PRIu32".%"PRIu32"\n",
	       VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION ) != 14 ) {

    fprintf( stderr, "ERROR: could not get send version to server\n" );
    exit( EXIT_FAILURE );
  }
  fflush( toServer );

  /* play the game! */
  while( fgets( line, MAX_LINE_LEN, fromServer ) ) {

    /* ignore comments */
    if( line[ 0 ] == '#' || line[ 0 ] == ';' ) {
      continue;
    }

    len = readMatchState( line, game, &state );
    if( len < 0 ) {

      fprintf( stderr, "ERROR: could not read state %s", line );
      exit( EXIT_FAILURE );
    }

    if( stateFinished( &state.state ) ) {
      /* ignore the game over message */

      continue;
    }

    if( currentPlayer( game, &state.state ) != state.viewingPlayer ) {
      /* we're not acting */

      continue;
    }

    len = step(len, line, game, &state, &rng);

    if( fwrite( line, 1, len, toServer ) != len ) {

      fprintf( stderr, "ERROR: could not get send response to server\n" );
      exit( EXIT_FAILURE );
    }
    fflush( toServer );
  }

  return EXIT_SUCCESS;
}