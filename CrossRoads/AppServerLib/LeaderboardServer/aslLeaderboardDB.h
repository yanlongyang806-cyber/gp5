#pragma once

typedef struct Leaderboard Leaderboard;
typedef struct LeaderboardData LeaderboardData;

void leaderboardDBInit(void);
void leaderboardDBAddToHog(Leaderboard *pLeaderboard);
void leaderboardDataDBAddToHog(LeaderboardData *pData);