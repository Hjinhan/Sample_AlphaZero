#if 1
#define DLL_API __declspec(dllexport)      
#else
#define DLL_API __declspec(dllimport)
#endif

#include<iostream>
#include <cstring>
#include <assert.h>

using namespace std;


typedef struct {
	int y, x;    // y 行  ，x 列
} Point;

typedef struct {
	int i, j;    // i 行， j列
} direction;


extern "C" {

	DLL_API void Init(int board_size_);
	DLL_API int is_finished(int *board, int *winner);
	DLL_API void encode(int *board, int *encode_state, int current_player, int state_channels, int last_act1, int last_act2);
	DLL_API int *restrain(int *encode_state, int *board, int current_player);

	/*void Init(int board_size_);
	int is_finished(int *board, int *winner);
	void encode(int *board, int *encode_state, int current_player, int state_channels, int last_act1, int last_act2);
	int *restrain_random(int *encode_state, int *board, int current_player);*/


	Point gp[361];
	direction dir[4];
	int       board_size, board_square;
	int       gn_cb;
	int       all_1[362], all_n1[362];


	void Init(int board_size_)

	{
		board_size = board_size_;
		board_square = board_size_*board_size_;


		for (int i = 0; i<board_square + 1; i++)
		{
			all_1[i] = 1; all_n1[i] = -1;
		}


		// 储存索引和位置
		int i1 = 0;
		for (int y = 0; y<board_size; y++) {
			for (int x = 0; x<board_size; x++) {
				gp[i1].y = y;
				gp[i1].x = x; i1++;
			}
		}

		//  ((1, -1), (1, 0), (1, 1), (0, 1))  
		/*
		0 0 0
		0 a 1    以 a 点开始 ， 为1的四个点方向搜索
		1 1 1
		*/

		dir[0].i = 1, dir[0].j = -1;
		dir[1].i = 1, dir[1].j = 0;
		dir[2].i = 1, dir[2].j = 1;
		dir[3].i = 0, dir[3].j = 1;

	}

	int is_finished(int *board, int *winner)
	{
		int has_legal_actions = 0, count, pos_t[5];
		for (int i = 0; i < board_square; i++)
		{
			if (board[i] == 0) { has_legal_actions = 1; continue; }
			if (board[i] == 0) { cout << "invalid board[i]!" << "\n"; assert(board[i] != 0); }
			int player = board[i];
			for (int j = 0; j < 4; j++)
			{
				int y = gp[i].y, x = gp[i].x;
				count = 0;
				for (int t = 0; t < 5; t++)
				{
					int pos = y * board_size + x;

					if (y < 0 || y >= board_size || x < 0 || x >= board_size) break;
					if (board[pos] != player) break;
					//	if (pos < 0 || pos >= board_square) { cout << "invalid pos!" << "\n"; assert(pos >= 0 && pos < board_square);}
					y += dir[j].i, x += dir[j].j;
					count += 1;
					if (count == 5)
					{
						winner[0] = player;
	
						return (1);
					}
				}
			}

		}
		if (has_legal_actions == 0) { winner[0] = 0; return (1); }
		else if (has_legal_actions == 1) { winner[0] = 0; return (0); }

	}



	void encode(int *board, int *encode_state, int current_player, int state_channels, int last_act1, int last_act2)  // 12 个特征
	{

		//己方位置起始地址
		int *pos_self = (int *)&encode_state[0 * board_square];

		//己方1，2，3, 4(以上)连棋 起始地址
		int *p_self1 = (int *)&encode_state[1 * board_square];
		int *p_self2 = (int *)&encode_state[2 * board_square];
		int *p_self3 = (int *)&encode_state[3 * board_square];
		int *p_self4 = (int *)&encode_state[4 * board_square];

		//对方位置起始地址
		int *pos_opp = (int *)&encode_state[5 * board_square];

		//对方1，2，3, 4(以上)连棋 起始地址
		int *p_opp1 = (int *)&encode_state[6 * board_square];
		int *p_opp2 = (int *)&encode_state[7 * board_square];
		int *p_opp3 = (int *)&encode_state[8 * board_square];
		int *p_opp4 = (int *)&encode_state[9 * board_square];

		//  last_act1 起始地址
		int *p_act1 = (int *)&encode_state[10 * board_square];

		//  last_act2 起始地址
		int *p_act2 = (int *)&encode_state[11 * board_square];

		//位置
		for (int i = 0; i < board_square ; i++)
		{ 
			if (board[i] == current_player) { pos_self[i] = 1;}
			if (board[i] == -current_player) { pos_opp[i] = 1;}
		}

		//动作
		if (last_act1 != -1) p_act1[last_act1] = 1;
		if (last_act2 != -1) p_act2[last_act2] = 1;

		int count, pos_t[5], pos1;

		//---------------------------------------连棋

		for (int i = 0; i < board_square; i++)
		{
			if (board[i] == 0) continue;
			if (board[i] == 0) { cout << "invalid board[i]!" << "\n"; assert(board[i] != 0); }
			int player = board[i];
			//	cout << "player is " << player <<"\n";
			for (int j = 0; j < 4; j++)     //    从该点的 左下、中下、右下、中右 搜索
			{
				int y = gp[i].y, x = gp[i].x;
				count = 0;
				memset(pos_t, -1, 5 * 4);     // -1 或者 0 可以直接置 ， 但是 1 不行 

				for (int t = 0; t < 5; t++)   // count 连棋数目， pos_t相应围棋
				{
					int pos = y * board_size + x;
					
					if (y < 0 || y >= board_size || x < 0 || x >= board_size) break;
					if (board[pos] != player) break;
					if (pos < 0 || pos >= board_square) { cout << "invalid pos!" << "\n"; assert(pos >= 0 && pos < board_square); }

					pos_t[count] = pos;
					y += dir[j].i, x += dir[j].j;
					count += 1;

				}

				if (count == 1 && player == current_player)  // 连棋数为1， 且 该点不为连棋数为2、3、4以上的点。相应位置置1
				{
					pos1 = pos_t[0];
					if (p_self2[pos1] == 0 && p_self3[pos1] == 0 && p_self4[pos1] == 0)
					{
						p_self1[pos1] = 1;
					}
				}

				else if (count == 1 && player == -1 * current_player)
				{
					pos1 = pos_t[0];
					if (p_opp2[pos1] == 0 && p_opp3[pos1] == 0 && p_opp4[pos1] == 0)
					{
						p_opp1[pos1] = 1;
					}
				}

				else if (count == 2 && player == current_player)  // 连棋数为2， 且 这些点 不为连棋数为3、4以上的点。 相应位置置1， 并在连棋数为1的平面将该点在置0
				{
					for (int a = 0; a < count; a++)
					{
						pos1 = pos_t[a];
						if (p_self3[pos1] == 0 && p_self4[pos1] == 0)
						{
							p_self1[pos1] = 0;
							p_self2[pos1] = 1;
						}
					}
				}

				else if (count == 2 && player == -1 * current_player)
				{
					for (int a = 0; a < count; a++)
					{
						pos1 = pos_t[a];
						if (p_opp3[pos1] == 0 && p_opp4[pos1] == 0)
						{
							p_opp1[pos1] = 0;
							p_opp2[pos1] = 1;
						}
					}
				}

				else if (count == 3 && player == current_player)
				{
					for (int a = 0; a < count; a++)
					{
						pos1 = pos_t[a];
						if (p_self4[pos1] == 0)
						{
							p_self1[pos1] = 0;
							p_self2[pos1] = 0;
							p_self3[pos1] = 1;
						}
					}
				}

				else if (count == 3 && player == -1 * current_player)
				{
					for (int a = 0; a < count; a++)
					{
						pos1 = pos_t[a];
						if (p_opp4[pos1] == 0)
						{
							p_opp1[pos1] = 0;
							p_opp2[pos1] = 0;
							p_opp3[pos1] = 1;
						}
					}
				}

				else if ((count == 4 || count == 5) && player == current_player)
				{
					for (int a = 0; a < count; a++)
					{
						pos1 = pos_t[a];
						p_self1[pos1] = 0;
						p_self2[pos1] = 0;
						p_self3[pos1] = 0;
						p_self4[pos1] = 1;
					}
				}

				else if ((count == 4 || count == 5) && player == -1 * current_player)
				{
					for (int a = 0; a < count; a++)
					{
						pos1 = pos_t[a];
						p_opp1[pos1] = 0;
						p_opp2[pos1] = 0;
						p_opp3[pos1] = 0;
						p_opp4[pos1] = 1;
					}
				}
			}
		}
	}



	int *restrain(int *encode_state, int *board, int current_player)
	{

		//己方位置起始地址
		int *pos_self = (int *)&encode_state[0 * board_square];

		//己方1，2，3, 4(以上)连棋 起始地址
		int *p_self1 = (int *)&encode_state[1 * board_square];
		int *p_self2 = (int *)&encode_state[2 * board_square];
		int *p_self3 = (int *)&encode_state[3 * board_square];
		int *p_self4 = (int *)&encode_state[4 * board_square];

		//对方位置起始地址
		int *pos_opp = (int *)&encode_state[5 * board_square];

		//对方1，2，3, 4(以上)连棋 起始地址
		int *p_opp1 = (int *)&encode_state[6 * board_square];
		int *p_opp2 = (int *)&encode_state[7 * board_square];
		int *p_opp3 = (int *)&encode_state[8 * board_square];
		int *p_opp4 = (int *)&encode_state[9 * board_square];


		int sign1 = 0, sign2 = 0, sign3 = 0;
		int pos_t[5], pos1;
		int count1 = 0, count2 = 0, count3 = 0, count4;
		static int  act_pos[45];
		memset(act_pos, -1, 45 * 4);

		/*	for (int i = 0; i < board_square; i++)
		{
		if (p_self4[i] == 1) { sign1 = 1; break;}
		}*/

		for (int i = 0; i < board_square; i++)   //己方有四个连棋， 且同一线上外应有至少1个空点
		{

			if (p_self4[i] == 0)  continue;

			for (int j = 0; j < 4; j++)
			{
				int y = gp[i].y, x = gp[i].x;
				count1 = 0;
				for (int t = 0; t < 4; t++)    // 搜索4连棋的位置，及 dir方向
				{
					int pos = y * board_size + x;
					
					if (y < 0 || y >= board_size || x < 0 || x >= board_size) break;
					if (p_self4[pos] == 0) break;
				
					pos_t[count1] = pos;
					y += dir[j].i, x += dir[j].j;
					count1 += 1;
				}       
				if (count1 == 4)
				{
					if (count1 != 4) { cout << "invalid, count1 != 4!" << "\n"; assert(count1 == 4); }
					count2 = 0;
					int po1, po2, po3;
					int tt[2];
					int des1 = 0, des2 = 0;
					for (int g = 0; g < 4; g++)   //  搜寻 四连棋 线上的空点 ，堵棋位置
					{
						pos1 = pos_t[g];
						int y1 = gp[pos1].y, x1 = gp[pos1].x;

						int y2 = y1 + dir[j].i, x2 = x1 + dir[j].j;
						
						int y3 = y1 - dir[j].i , x3 = x1 - dir[j].j;  // y3 ，x3 反方向
						if (dir[j].i == 0) y3 = y1;
						if (dir[j].j == 0)  x3 = x1;

						po1 = y2 * board_size + x2;
						po2 = y3 * board_size + x3;
						if (y2 < 0 || y2 >= board_size || x2 < 0 || x2 >= board_size) des1 = 1;
						if (y3 < 0 || y3 >= board_size || x3 < 0 || x3 >= board_size) des2 = 1;

						if (des1 == 0 && board[po1] == 0)
						{
							count2 += 1;
						}

						if (des2 == 0 && board[po2] == 0)
						{
							count2 += 1;
						}

					}

					if (count2 >= 1)
					{
						sign1 = 1;
					}

				}

			}
		}


		for (int i = 0; i < board_square; i++)    //对方有四个连棋， 且同一线上外应有至少1个空点
		{

			if (p_opp4[i] == 0)  continue;

			for (int j = 0; j < 4; j++)
			{
				int y = gp[i].y, x = gp[i].x;
				count1 = 0;
				for (int t = 0; t < 4; t++)    // 搜索4连棋的位置，及 dir方向
				{
					int pos = y * board_size + x;
					pos_t[count1] = pos;
					if (y < 0 || y >= board_size || x < 0 || x >= board_size) break;
					if (p_opp4[pos] == 0) break;
					y += dir[j].i, x += dir[j].j;
					count1 += 1;
				}
				if (count1 == 4)
				{
					if (count1 != 4) { cout << "invalid, count1 != 4!" << "\n"; assert(count1 == 4); }
					count2 = 0;
					int po1, po2, po3;
					int tt[2];
					int des1 = 0, des2 = 0;
					for (int g = 0; g < 4; g++)   //  搜寻 三连棋 线上的空点 ，堵棋位置
					{
						pos1 = pos_t[g];
						int y1 = gp[pos1].y, x1 = gp[pos1].x;

						int y2 = y1 + dir[j].i, x2 = x1 + dir[j].j;

						int y3 = y1 - dir[j].i, x3 = x1 - dir[j].j;  // y3 ，x3 反方向
						if (dir[j].i == 0) y3 = y1;
						if (dir[j].j == 0)  x3 = x1;

						po1 = y2 * board_size + x2;
						po2 = y3 * board_size + x3;
						if (y2 < 0 || y2 >= board_size || x2 < 0 || x2 >= board_size) des1 = 1;
						if (y3 < 0 || y3 >= board_size || x3 < 0 || x3 >= board_size) des2 = 1;

						if (des1 == 0 && board[po1] == 0)
						{
							count2 += 1;
						}

						if (des2 == 0 && board[po2] == 0)
						{
							count2 += 1;
						}

					}

					if (count2 >= 1)
					{
						sign2 = 1;
					}

				}

			}
		}



		if (sign1 == 0 && sign2 == 0)   // 己方有3个连棋， 且同一线上外应有至少2个空点
		{

			for (int i = 0; i < board_square; i++)
			{

				if (pos_self[i] == 0)  continue;   // 注意·这里 使用 pos_self  ，不是p_self3

				for (int j = 0; j < 4; j++)
				{
					int y = gp[i].y, x = gp[i].x;
					count1 = 0;
					for (int t = 0; t < 3; t++)    // 搜索三连棋的位置，及 dir方向
					{
						int pos = y * board_size + x;
						
						if (y < 0 || y >= board_size || x < 0 || x >= board_size) break;
						if (pos_self[pos] == 0) break;
					
						pos_t[count1] = pos;
						y += dir[j].i, x += dir[j].j;
						count1 += 1;
					}
					if (count1 == 3)
					{
						if (count1 != 3) { cout << "invalid, count1 != 3!" << "\n"; assert(count1 == 3); }
						count2 = 0;
						int po1, po2, po3;
						int tt[2];
						int des1 = 0, des2 = 0;
						for (int g = 0; g < 3; g++)   //  搜寻 三连棋 线上的空点 ，堵棋位置
						{
							pos1 = pos_t[g];
							int y1 = gp[pos1].y, x1 = gp[pos1].x;

							int y2 = y1 + dir[j].i, x2 = x1 + dir[j].j;

							int y3 = y1 - dir[j].i, x3 = x1 - dir[j].j;  // y3 ，x3 反方向
							if (dir[j].i == 0) y3 = y1;
							if (dir[j].j == 0)  x3 = x1;

							po1 = y2 * board_size + x2;
							po2 = y3 * board_size + x3;
							if (y2 < 0 || y2 >= board_size || x2 < 0 || x2 >= board_size) des1 = 1;
							if (y3 < 0 || y3 >= board_size || x3 < 0 || x3 >= board_size) des2 = 1;

							if (des1 == 0 && board[po1] == 0)
							{
								count2 += 1;
							}

							if (des2 == 0 && board[po2] == 0)
							{
								count2 += 1;
							}

						}

						if (count2 == 2)
						{
							sign3 = 1;
						}

					}

				}
			}

		}


		if (sign1 == 0 && sign2 == 0 && sign3 == 0) {  // 不满足上面条件 ， 堵住对方三连棋（线上有两个空点，且这两点外面没有己方棋子或者是不合法的点 

			for (int i = 0; i < board_square; i++)
			{

				if (pos_opp[i] == 0)  continue;

				for (int j = 0; j < 4; j++)
				{
					int y = gp[i].y, x = gp[i].x;
					count1 = 0;
					for (int t = 0; t < 3; t++)    // 搜索三连棋的位置，及 dir方向
					{
						int pos = y * board_size + x;
						
						if (y < 0 || y >= board_size || x < 0 || x >= board_size) break;
						if (pos_opp[pos] == 0) break;

						pos_t[count1] = pos;
						y += dir[j].i, x += dir[j].j;
						count1 += 1;
					}

					if (count1 == 3)
					{
						//cout << "count1=  " << count1 << "\n";
						if (count1 != 3) { cout << "invalid, count1 != 3!" << "\n"; assert(count1 == 3); }
						count2 = 0;
						count4 = 0;
						int po1, po2, po3, po12, po22;
						int tt[2];
						int des1 = 0, des2 = 0;
						for (int g = 0; g < 3; g++)   //  搜寻 三连棋 线上的空点 ，堵棋位置
						{
							pos1 = pos_t[g];
							int y1 = gp[pos1].y, x1 = gp[pos1].x;

							int y3, x3, y32, x32;
							int y2 = y1 + dir[j].i, x2 = x1 + dir[j].j;
							int y22 = y2 + dir[j].i, x22 = x2 + dir[j].j;


							if (dir[j].i == 1) { y3 = y1 - 1; y32 = y3 - 1; } // y3 ，x3 反方向
							else if (dir[j].i == 0) { y3 = y1; y32 = y3; }

							if (dir[j].j == 1) { x3 = x1 - 1; x32 = x3 - 1; }
							else if (dir[j].j == -1) { x3 = x1 + 1; x32 = x3 + 1; }
							else if (dir[j].j == 0) { x3 = x1; x32 = x3; }

							po1 = y2 * board_size + x2;
							po2 = y3 * board_size + x3;

							po12 = y22 * board_size + x22;
							po22 = y32 * board_size + x32;

							if (y2 < 0 || y2 >= board_size || x2 < 0 || x2 >= board_size) des1 = 1;
							if (y3 < 0 || y3 >= board_size || x3 < 0 || x3 >= board_size) des2 = 1;

							/*	cout << "pos1= " << pos1 << "\n";
							cout << "po1= " << po1 << "  po2= " << po2 << "\n";*/
							if (des1 == 0 && board[po1] == 0)
							{
								tt[count2] = po1;
								count2 += 1;
								if (y22 < 0 || y22 >= board_size || x22 < 0 || x22 >= board_size || board[po12] == current_player)
									count4 += 1;
							}

							if (des2 == 0 && board[po2] == 0)
							{
								tt[count2] = po2;
								count2 += 1;
								if (y32 < 0 || y32 >= board_size || x32 < 0 || x32 >= board_size || board[po22] == current_player)
									count4 += 1;
							}

						}
					/*	cout << "count2=  " << count2 << "\n";
						cout << "count4=  " << count4 << "\n";*/
						//cout << "tt1=  " << tt[0] << "\n";
						if (count2 == 2 && count4 != 2)
						{
							for (int b = 0; b < count2; b++)
							{
								act_pos[count3] = tt[b];
								count3 += 1;
							}
						}

					}

				}
			}
		}
		return(act_pos);

	}


}

