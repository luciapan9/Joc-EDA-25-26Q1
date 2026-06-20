#include "Player.hh"

#define PLAYER_NAME Yuta5

struct PLAYER_NAME : public Player {

  static Player *factory() {
    return new PLAYER_NAME;
  }

  typedef vector<vector<int>> VVI;
  typedef vector<int> VI;

  const vector<Dir> Dirs = {Up,Down,Left,Right};
  const int MAX_SOLDIERS = 1; 

  int caja_objetivo; 

  map<int,int> cont; 
  map<int,Pos> box_positions; 

  struct Trip {
    int cont;
    int v[3];
  };

  enum Priority_Profesor {
    caja_fuerte,   
    dinero,        
    kit_veneno,    
    kit_salud      
  };

  enum Priority_soldiers {
    block_box,
    matar_profesor,
    matar_enemigo,     
    matar,   
    busco_dinero,  
    busco_veneno,  
    busco_salud    
  };

  struct Opcion {
    Dir dir;           
    int dist;        
    int prioridad;
    int caja_id;
    int type;
    
    bool operator<(const Opcion& other) const {
        if (prioridad != other.prioridad) return prioridad > other.prioridad;
        return dist > other.dist; 
    }
  };

 
  /**
   * Comprueba si en la posición p hay un soldado mío
   */
  bool is_my_soldier(Pos &p) const {
    int id = cell(p).id;
    if (id == -1) return false;
    Unit u = unit(id);
    return u.player == me();
  }

  /**
   * Comprueba si la posición p está vacía (no hay ningún soldado,ni míos)
   */
  bool is_empty(Pos &p) const {
    return cell(p).id == -1;
  }

  /**
   * Comprueba si la posición p está dentro del tablero y no es pared
   */
  bool is_valid(Pos &p) const {
    return pos_ok(p) and cell(p).type == Corridor;
  }

  /**
   * Comprueba si en la posicion p hay un enemigo
   */
  bool has_enemy(Pos &p) const {
    return cell(p).id != -1 and unit(cell(p).id).player != me();
  }

  /**
   * Comprueba si mi profesor esta vivo,
   */
  bool professor_is_alive() const {
    int id_prof = professor(me());
    Unit prof = unit(id_prof);
    return prof.is_alive();
  }

  /**
   * Comprueba si mi profesor esta vivo, y sino si aparece antes de la ronda 185
   */
  bool professor_is_valid() const {
    int id_prof = professor(me());
    Unit prof = unit(id_prof);
    if (prof.is_alive()) return true;
    return round() + prof.rounds_for_reborn <= 184;
  }

  /**
   * Comprueba si en la posición p hay un profesor enemigo
   */
  bool professor_enemy(Pos p) const {
    int id = cell(p).id;
    if (id == -1) return false;
    Unit u = unit(id);
    return u.type == Professor and u.player != me();
  }

  /**
   * Comprueba si la posición p es la entrada de una caja fuerte
   */
  bool is_safe_door(Pos p, int& id_box) {
    if (not is_valid(p)) return false;
    Cell c = cell(p);
    if (c.box == -1 or c.gold) return false;
    // Debe tener oro en una casilla cardinal
    for (Dir d : Dirs) {
        Pos o = p + d;
        if (pos_ok(o) and cell(o).gold) {
          id_box = c.box;
          return true;
        }
    }
    return false;
    }

  /**
   * Devuelve una posición aleatoria 
   */
  Dir movimiento_aleatorio(Pos& p) {
      for (int i = 0; i < int(Dirs.size()); ++i) {
          Dir dir = Dirs[random(0,Dirs.size()-1)];
          Pos next = p + dir;
          if (is_valid(next)) {
              return dir;
          }
      }
      return Right;
  }
  
  /**
   * Devuelve si es posible una posición aleatoria tal que sus adyacentes no tiene enemigos.
   * Si no existe, devuelve una posición aleatoria.
   */
  Dir movimiento_aleatorio_seguro(Pos& p) {
    Dir last_option = Right;
    for (int i = 0; i < (int)Dirs.size(); ++i) {
        Dir dir = Dirs[random(0,Dirs.size()-1)];
        Pos next = p + dir;
        if (is_valid(next) and not has_enemy(next)) {
          // miramos sus adyacentes a ver si hay enemigos
          bool safe = true;
          for (Dir d2 : Dirs) {
            Pos adj = next + d2;
            if (is_valid(adj) and has_enemy(adj)) {
              safe = false;
              last_option = dir; // guardamos como última opción
              break;
            }
          }
          if (safe) return dir; // primera opción totalmente segura
        }
    }
    return last_option;
  }

  /**
   * Comprueba si a dist == 2 de la Pos p hay enemigo soldado con veneno
   */

  bool is_dangerous_enemy(Pos p, Dir d) {
    Pos p_intermedio = p + d;
    for (Dir d2 : Dirs) {
      Pos adj_enemy = p_intermedio + d2;
      if (adj_enemy == p) continue;
      if (is_valid(adj_enemy) and has_enemy(adj_enemy)) {
        if (unit(cell(adj_enemy).id).poison > 0) return true;
      }
    }
    return false;
  }
  

  // ======================================
  //      RASTREO INICIAL DE LAS CAJAS
  // ======================================

  void initial_research() {
    box_positions.clear();
    int n = board_rows();
    int m = board_cols();
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < m; ++j) {
        Pos p(i,j);
        int id_box = -1;
        if (is_valid(p) and is_safe_door(p,id_box)) {
          box_positions[id_box] = p;
          if (cont.find(id_box) == cont.end()) cont[id_box] = 0;
        }
        if (box_positions.size() == 6) return; // ya hemos encontrado las 6 cajas, no hace falta seguir
      }
    }
  }


  // ===========================
  //      FUNCIONES BFS
  // ===========================

  pair<Dir,int> bfs_soldiers(Pos inicio, Priority_soldiers prio, int max_dist, int& caja_bloqueo) {
    queue<pair<Pos,Dir>> Q;
    VVI vis(board_rows(),VI(board_cols(),-1));

    Q.push({inicio, Right});
    vis[inicio.i][inicio.j] = 0;
    Pos reserved_pos = unit(professor(me())).pos;

    bool profe_cerca = false;
    if (prio == block_box) {
      if (caja_objetivo != -1 and box_positions.count(caja_objetivo)) {
        Pos p_obj = box_positions[caja_objetivo];
        int dist_prof = abs(reserved_pos.i - p_obj.i) + abs(reserved_pos.j - p_obj.j);
        if (dist_prof <= 4) profe_cerca = true;
      }
    }

    while (not Q.empty()) {
      Pos actual = Q.front().first;
      Dir d = Q.front().second;
      Q.pop();
      int dist = vis[actual.i][actual.j];
      int id_box = -1;

      switch (prio) {
        case block_box:
          if (is_safe_door(actual, id_box) and cont[id_box] < MAX_SOLDIERS) {
            if (id_box != caja_objetivo) {
              caja_bloqueo = id_box;
              return {d,dist};
            }
            else {
              bool amenaza_enemiga = false;
              for (Dir d_check : Dirs) {
                Pos ady = actual + d_check;
                if (is_valid(ady) and professor_enemy(ady)) amenaza_enemiga = true;
              }
              if (not profe_cerca or amenaza_enemiga) {
                caja_bloqueo = id_box;
                return {d,dist};
              }
            }
          }
          break;

        case matar_profesor:
            if (has_enemy(actual) and professor_enemy(actual)) return {d,dist};
            break;

        case matar_enemigo:
            if (has_enemy(actual)) return {d,dist};
            break; 

        case matar:
          if (has_enemy(actual) or professor_enemy(actual)) return {d,dist};
          break;

        case busco_dinero:
            if (cell(actual).money) return {d,dist};
            break;

        case busco_veneno:
            if (cell(actual).poison_kit) return {d,dist};
            break;

        case busco_salud:
            if (cell(actual).health_kit) return {d,dist};
            break;

        default: break;
      }

      for (Dir dir : Dirs) {
        Pos next = actual + dir;
        int next_dist = dist + 1;
        if (next_dist > max_dist) continue;
        if (profe_cerca and caja_objetivo != -1 and next == box_positions[caja_objetivo]) continue;
        
        if (is_valid(next) and vis[next.i][next.j] == -1 and next != reserved_pos) {
            if (cell(next).id != -1) {
              Unit u = unit(cell(next).id);
              if (u.player == me()) continue;
            }
            if (has_enemy(next)) {
                if (prio != matar_enemigo and prio != matar_profesor and prio != matar) continue;
            }
            
            vis[next.i][next.j] = next_dist;
            Dir dist_next = (dist == 0 ? dir : d);
            Q.push({next, dist_next});
        }
      }
    }
    return {Right, -1};
  }

  pair<Dir,int> bfs_professor(Pos inicio,Priority_Profesor prio,int& id_box,int max_dist) {
    queue<pair<Pos,Dir>> Q;
    VVI vis(board_rows(),VI(board_cols(),-1));

    Q.push({inicio,Right});
    vis[inicio.i][inicio.j] = 0;

    while (not Q.empty()) {
      Pos actual = Q.front().first;
      Dir d = Q.front().second;
      Q.pop();

      int dist = vis[actual.i][actual.j];

      // --- comprobamos objetivos ---
      switch (prio) {
        case caja_fuerte:
          if (is_safe_door(actual,id_box) and not has_enemy(actual)) return {d,dist};
          break;

        case dinero:
          if (cell(actual).money) return {d,dist};
          break;

        case kit_veneno:
          if (cell(actual).poison_kit) return {d,dist};
          break;

        case kit_salud:
          if (cell(actual).health_kit) return {d,dist};
          break;
          
        default:
            break;
      }

      // --- expandimos vecinos ---
      for (Dir dir : Dirs) {
        Pos next = actual + dir;
        if (not is_valid(next)) continue;
        if (vis[next.i][next.j] != -1) continue;
        if (has_enemy(next)) continue;

        int next_dist = dist + 1;
        if (next_dist > max_dist) continue;
        vis[next.i][next.j] = next_dist;
            
        Dir dist_next = (actual == inicio ? dir : d);
        Q.push({next, dist_next});
      }
    }
    return {Left, 0};
  }

  pair<Dir,int> bfs_to_target(Pos start, Pos target) {
    if (start == target) return {Right, 0};
    
    queue<pair<Pos,Dir>> Q;
    VVI vis(board_rows(),VI(board_cols(),-1));

    Q.push({start, Right});
    vis[start.i][start.j] = 0;
    Pos reserved_pos = unit(professor(me())).pos;
    int dummy = -1;

    while (not Q.empty()) {
      Pos actual = Q.front().first;
      Dir d = Q.front().second;
      Q.pop();
      int dist = vis[actual.i][actual.j];

      if (actual == target) {
        if (is_safe_door(actual,dummy))   return {d, dist};
        return {d,-1};
      }

      for (Dir dir : Dirs) {
        Pos next = actual + dir;
        int next_dist = dist + 1;
        
        if (not pos_ok(next)) continue;
        if (cell(next).type == Wall) continue; 
        if (cell(next).id != -1 && next != target) continue;
        if (vis[next.i][next.j] != -1) continue;
        if (next == reserved_pos) continue;

        if (cell(next).id != -1) continue; 

        vis[next.i][next.j] = next_dist;
        Dir dist_next = (actual == start ? dir : d);
        Q.push({next, dist_next});
      }
    }
    return {Right, -1};
  }

  // =========================
  //    MOVIMIENTO SOLDADOS
  // =========================

  void move_soldiers() {
    vector<int> my_soldiers = soldiers(me());
    set<int> moved_soldiers;
    vector<int> last_moves;
    
    // movemos mis soldados con veneno con enemigo adyacente primero
    for (int id : my_soldiers) {
      Unit soldier = unit(id);
      if (soldier.poison > 0) {
        for (Dir d : Dirs) {
          Pos next = soldier.pos + d;
          if (pos_ok(next) and has_enemy(next)) {
            move(id, d);
            moved_soldiers.insert(id);
            break;
          }
        }
      }
    }

    // movemos el resto de soldados
    for (int id : my_soldiers) {
      if (moved_soldiers.count(id)) continue;
      Unit u = unit(id);
      
      int dummy = -1; 

      // recargamos de acuerdo a ciertas condiciones
      if (u.poison == 0 and available_poison(me()) > 0) {
        bool need = false;
        for (Dir d : Dirs) {
          Pos next = u.pos + d;
          if (is_valid(next) and (professor_enemy(next) or has_enemy(next))) {
            need = true;
            break;
          }
        }
        if (not need) {
           pair<Dir,int> bfs_peligro = bfs_soldiers(u.pos, matar, 6, dummy);
           if (bfs_peligro.second != -1) need = true;
        }
        if (need) {
           charge(id);
           moved_soldiers.insert(id);
           continue;
        }
      }

      // Definimos las prioridades
      int prio_urgencia      = 1; 
      int prio_opportunism   = 8;  
      int base_recursos      = 10; 
      int base_caja_cercana  = 5;
      int base_caja_lejana   = 25; 
      int base_enemigo_lejos = 1000; 
      int base_profesor      = 1000; 

      priority_queue<Opcion> PQ;

      pair<Dir,int> bfs_dinero = bfs_soldiers(u.pos, busco_dinero, 30, dummy);
      if (bfs_dinero.second != -1) {
        PQ.push({bfs_dinero.first, bfs_dinero.second, base_recursos + bfs_dinero.second, -1, 0});
      }

      pair<Dir,int> bfs_veneno = bfs_soldiers(u.pos, busco_veneno, 30, dummy);
      if (bfs_veneno.second != -1) {
        PQ.push({bfs_veneno.first, bfs_veneno.second, base_recursos + bfs_veneno.second, -1, 0});
      }

      if (professor_is_valid()) {
        pair<Dir,int> bfs_salud = bfs_soldiers(u.pos, busco_salud, 17, dummy);
        if (bfs_salud.second != -1) {
          PQ.push({bfs_salud.first, bfs_salud.second, base_recursos + bfs_salud.second, -1, 0});
        }
      }


      for (auto const& [id_box, pos_box] : box_positions) {
          if (cont[id_box] >= MAX_SOLDIERS) continue;

          // --- RADAR DE AMENAZA ---
          bool hay_profe_enemigo_cerca = false;
          for (int pl = 0; pl < num_players(); ++pl) {
              if (pl == me()) continue;
              int id_p = professor(pl);
              if (unit(id_p).is_alive()) {
                int d = abs(unit(id_p).pos.i - pos_box.i) + abs(unit(id_p).pos.j - pos_box.j);
                // Si hay un profesor enemigo a menos de 30 pasos, ES PELIGROSO
                if (d <= 30) {
                hay_profe_enemigo_cerca = true;
                break;
                }
              }
          }

          // Si NO hay nadie cerca queriendo robarla, PASAMOS de taparla.
          if (!hay_profe_enemigo_cerca) continue; 

          // Filtro de mi propio profesor
          Pos pos_profe = unit(professor(me())).pos;
          int dist_profe_box = abs(pos_profe.i - pos_box.i) + abs(pos_profe.j - pos_box.j);
          if (id_box == caja_objetivo and dist_profe_box <= 3) continue;

          // Filtro distancia soldado
          int dist_man = abs(u.pos.i - pos_box.i) + abs(u.pos.j - pos_box.j);
          if (dist_man > 40) continue; 

          pair<Dir,int> res = bfs_to_target(u.pos, pos_box);
          if (res.second != -1) {
              int p_final;
              int dist = res.second;
              if (dist <= 12) p_final = base_caja_cercana + dist;
              else p_final = base_caja_lejana + dist;

              PQ.push({res.first, dist, p_final, id_box, 1});
          }
      }


      // 3. COMBATE
      if (u.poison == 0) {
        pair<Dir,int> bfs_recarga = bfs_soldiers(u.pos, busco_veneno, 20, dummy);
        if (bfs_recarga.second != -1) {
             PQ.push({bfs_recarga.first, bfs_recarga.second, prio_urgencia + bfs_recarga.second, -1, 0});
        }
      }
      else {
        pair<Dir,int> bfs_prof = bfs_soldiers(u.pos, matar_profesor, 18, dummy);
        if (bfs_prof.second != -1) {
             int p = (bfs_prof.second <= 2) ? prio_urgencia : base_profesor;
             PQ.push({bfs_prof.first, bfs_prof.second, p, -1, 0});
        }

        pair<Dir,int> bfs_enemigo = bfs_soldiers(u.pos, matar_enemigo, 30, dummy);
        if (bfs_enemigo.second != -1) {
             int p = (bfs_enemigo.second <= 2) ? prio_urgencia : base_enemigo_lejos;
             PQ.push({bfs_enemigo.first, bfs_enemigo.second, p, -1, 2});
        }
      }
      
      // DECISIÓN
      bool se_movio = false;
      while (not PQ.empty()) {
        Opcion best = PQ.top();
        PQ.pop();

        if (best.caja_id != -1) {
          if (cont[best.caja_id] >= MAX_SOLDIERS) continue;
          ++cont[best.caja_id];
          move(id, best.dir);
          moved_soldiers.insert(id);
          se_movio = true;
          break;
        }
        else if (best.type == 2 and best.dist == 2 and is_dangerous_enemy(u.pos, best.dir)) {
             last_moves.push_back(id);
             moved_soldiers.insert(id);
             se_movio = true;
             break;
        }
        else {
          move(id, best.dir);
          moved_soldiers.insert(id);
          se_movio = true;
          break;
        }
      }
      
      if (!se_movio) {
          move(id, movimiento_aleatorio_seguro(u.pos));
          moved_soldiers.insert(id);
      }
    }
    
    // WAITING
    for (int id : last_moves) {
       Unit u = unit(id);
       for (Dir d : Dirs) {
           Pos next = u.pos + d;
           if (pos_ok(next) and has_enemy(next)) {
               move(id, d);
               break;
           }
       }
    }
  }
  
  // ===========================
  //    MOVIMIENTO PROFESOR
  // ===========================
 
  bool is_combination(int x,int y,int z) const {
    return (x + y == z) or (x + z == y) or (z + y == x);
  }

  bool backtracking(const VI &hints,VI &solution,vector<Trip> &T, int i) {
    int M = hints.size();
    int n = T.size();
    if (i == M) return true;

    int hint = hints[i];
    for (int j = 0; j < n; ++j) {
      if (T[j].cont < 3) {
        bool compleix = true;
        if (T[j].cont == 2) {
          if (not is_combination(T[j].v[0],T[j].v[1],hint)) compleix = false;
        }
        if (compleix) {
          T[j].v[T[j].cont++] = hint;
          solution[i] = j;
          if (backtracking(hints,solution,T,i + 1)) {
            return true;
          }
          --T[j].cont;
        }
      }
    }
    return false; 
  }

  void move_profesor() {
    int id = professor(me());
    Unit profesor = unit(id);
    Pos p = profesor.pos;
    bool moved = false;

    if (profesor.health <= 40 and available_health(me()) >= 5) {
      heal(id);
      moved = true;
    }

    int id_box = -1;
    if (not moved) {
      pair<Dir,int> bfs_box = bfs_professor(p,caja_fuerte,id_box,50);
      if (bfs_box.second == 1) {
        vector<int> hints = safety_box_hints(id_box);
        int M = hints.size();
        int n = M / 3;
        vector<int> sol(M);
        vector<Trip> T(n);

        if (backtracking(hints,sol,T,0)) {
          open(id,sol,bfs_box.first);
          caja_objetivo = -1;
          moved = true;
        }
      } 
      else if (not moved and bfs_box.second > 1) {
        caja_objetivo = id_box;
        move(id,bfs_box.first);
        moved = true;
      }
    }

    if (not moved) {
      pair<Dir,int> bfs_money = bfs_professor(p,dinero,id_box,5);
      if (bfs_money.second > 0) {
        move(id,bfs_money.first);
        moved = true;
      }
    }

    if (not moved) {
      pair<Dir,int> bfs_poison = bfs_professor(p,kit_veneno,id_box,6);
      if (bfs_poison.second > 0) {
        move(id,bfs_poison.first);
        moved = true;
      }
    }

    if (not moved) {
      pair<Dir,int> bfs_salud = bfs_professor(p,kit_salud,id_box,5);
      if (bfs_salud.second > 0) {
        move(id, bfs_salud.first);
        moved = true;
      }
    }

    if (not moved) {
      Dir d = movimiento_aleatorio_seguro(p);
      move(id,d);
    }
  }


  // ===========================
  //          MAIN
  // ===========================
  virtual void play() {
    // If nearly out of time, do nothing.
    double st = status(me());
    if (st >= 0.9) return;

    cont.clear(); 
    initial_research();
    move_soldiers();
    move_profesor();
  }
};


/**
 * Do not modify the following line.
 */
RegisterPlayer(PLAYER_NAME);