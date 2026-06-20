#include "Player.hh"

#define PLAYER_NAME Yuta

struct PLAYER_NAME : public Player {

  static Player *factory() {
    return new PLAYER_NAME;
  }

  typedef vector<vector<int>> VVI;
  typedef vector<int> VI;

  const vector<Dir> Dirs = {Up,Down,Left,Right};
  const int MAX_SOLDIERS = 1; // Máximo número de soldados que bloquean una caja

  int caja_objetivo; // id de la caja objetivo de mi profesor

  map<int,int> cont; // first = id_box, second = núm. de soldados que bloquean la caja
  map<int,Pos> box_positions; // first = id_box, second = position of the box

  struct Trip {
    int cont;
    int v[3];
  };

  enum Priority_Profesor {
    caja_fuerte,   // prioridad 0: abrir cajas fuertes
    dinero,        // prioridad 1 (dist<=2): buscamos dinero
    kit_veneno,    // prioridad 2 (dist<=2): buscamos veneno
    kit_salud      // prioridad 3 (dist<=2): buscamos kit's de salud
  };

  enum Priority_soldiers {
    block_box,     // prioridad 0: bloqueo de cajas fuertes
    ataque,        // prioridad 1: matar enemigos
    busco_dinero,  // priordiad 2: cogemos dinero
    busco_veneno,  // prioridad 3: buscamos veneno
    busco_salud    // prioridad 4: buscamos kit's de salud
  };


  struct Option {
    Dir dir;
    int dist;
    int id_caja;
  };


  // ===========================
  //    FUNCIONES AUXILIARES
  // ===========================

  // Funciones de territorio

  /**
   * Comprueba si en la posición p hay un soldado mío
   * (es solo para el profesor, por lo que solo miramos si en p hay alguien, y si hay alguien
   * que sea mío)
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
        if (is_valid(o) and cell(o).gold) {
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

  Option BFS_soldiers(Pos inicio,Priority_soldiers prio,int max_dist) {
    queue<pair<Pos,Dir>> Q;
    VVI vis(board_rows(),VI(board_cols(),-1));

    Q.push({inicio, Right});
    vis[inicio.i][inicio.j] = 0;
    Pos reserved_pos = unit(professor(me())).pos;   

    // el profesor tiene caja objetivo, calculamos su distancia respecto a la caja selecionada
    bool profe_cerca = false;
    if (prio == block_box) {
      if (caja_objetivo != -1 and box_positions.count(caja_objetivo)) {
        Pos p_obj = box_positions[caja_objetivo];
        int dist_prof = abs(reserved_pos.i - p_obj.i) + abs(reserved_pos.j - p_obj.j);
        if (dist_prof <= 4) profe_cerca = true;
      }
    }
    
    int id_box = -1;
    while (not Q.empty()) {
      Pos actual = Q.front().first;
      Dir d = Q.front().second;
      Q.pop();

      int dist = vis[actual.i][actual.j];

      // --- comprobamos objetivos ---
      switch (prio) {
        case ataque:
          if (has_enemy(actual) or professor_enemy(actual)) {
            // dist == 2 y es soldado enemigo
            if (dist == 2 and not professor_enemy(actual)) return {d,-9,-1};
            return {d,dist,-1};
          }
          break;

        case block_box:
          if (is_safe_door(actual, id_box) and cont[id_box] < MAX_SOLDIERS) {
            if (id_box != caja_objetivo) return {d,dist,id_box};
            else {
              bool amenaza_enemiga = false;
              for (Dir d_check : Dirs) {
                Pos ady = actual + d_check;
                if (is_valid(ady) and professor_enemy(ady)) {
                    amenaza_enemiga = true;
                    break;
                }
              }
              if (not profe_cerca or amenaza_enemiga) {
                return {d,dist,id_box};
              }
            }
          }

        case busco_dinero:
          if (cell(actual).money) return {d,dist,-1};
          //break;

        case busco_veneno:
          if (cell(actual).poison_kit) return {d,dist,-1};
          //break;

        case busco_salud:
          if (cell(actual).health_kit) return {d,dist,-1};
          //break;

        default:
        break;
      }
      // --- expandimos vecinos ---
      for (Dir dir : Dirs) {
        Pos next = actual + dir;
        if (not is_valid(next)) continue;
        if (vis[next.i][next.j] != -1) continue;
        if (next == reserved_pos) continue;
        if (profe_cerca and caja_objetivo != -1 and next == box_positions[caja_objetivo]) continue;

        int next_dist = dist + 1;
        if (next_dist > max_dist) continue;
        vis[next.i][next.j] = next_dist;
      
        // si estamos en inicio, la dirección inicial es dir
        // si no, heredamos d
        Dir dist_next = (actual == inicio ? dir : d);
        Q.push({next, dist_next});
      }
    }
    return {Right, -1,-1};
  }

  pair<Dir,int> BFS_profesor(Pos inicio,Priority_Profesor prio,int& id_box,int max_dist) {
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


  // =========================
  //    MOVIMIENTO SOLDADOS
  // =========================

  void move_soldiers() {
    vector<int> my_soldiers = soldiers(me());
    set<int> moved_soldiers;
    vector<int> last_moves;

    /**
     *  ---------------------------------------------------------------------------------- 
     *    Primero movemos mis soldados con veneno adyacentes a un enemigo para tener mas
     *    probabilidad de ser el que ataque primero y así ganarlos
     *  ----------------------------------------------------------------------------------
     */
    for (int ids : my_soldiers) {
      Unit soldier = unit(ids);
      if (soldier.poison) {
        for (Dir d : Dirs) {
          Pos new_pos = soldier.pos + d;
          if (is_valid(new_pos) and has_enemy(new_pos)) {
            move(ids,d);
            moved_soldiers.insert(ids);
            break;
          }
        }
      }
    }

    /**
     *  -------------------------------- 
     *    Movemos el resto de soldados
     *  --------------------------------  
     */
    for (int id : my_soldiers) {
      Unit soldier = unit(id);
      if (moved_soldiers.find(id) != moved_soldiers.end()) continue; 

      /**
       *  --------------------------------------------------------------------------- 
       *    Si no tiene veneno, el soldado recarga de acuerdo a ciertas condiciones
       *  ---------------------------------------------------------------------------
       */
      if (soldier.poison == 0 and available_poison(me()) > 0) {
        bool recargar = false;
        for (Dir d : Dirs) {
          Pos next = soldier.pos + d;
          if (is_valid(next) and (professor_enemy(next) or has_enemy(next))) {
           recargar = true;
           break;
          }
        }
        // tiene un enemigo dentro de un radio de 4    NOTA: QUIZÁS MEJOR
        if (not recargar) {
          Option enemigos = BFS_soldiers(soldier.pos,ataque,5);
          if (enemigos.dist > 0) recargar = true;
        }
        // recargamos veneno
        if (recargar) {
          charge(id);
          moved_soldiers.insert(id);
          continue;
        }
      }
      if (moved_soldiers.find(id) != moved_soldiers.end()) continue;

      /**
       *  -------------------------------------------------------------------------------
       *    A partir de aquí mis soldados se moverán de acuerdo al orden de prioridades
       *  -------------------------------------------------------------------------------  
       */
      Option bfs_block_box = BFS_soldiers(soldier.pos, block_box, 20);
      if (bfs_block_box.dist != -1) {
        if (bfs_block_box.dist > 0) move(id, bfs_block_box.dir);
        moved_soldiers.insert(id);
        ++cont[bfs_block_box.id_caja];
        continue;
      }
      
      // Prioridad 1: Buscamos enemigos en caso de tener veneno
      if (soldier.poison) {
        Option bfs_ataque = BFS_soldiers(soldier.pos,ataque,40);
        if (bfs_ataque.dist != -1) {
          move(id,bfs_ataque.dir);
          moved_soldiers.insert(id);
          continue;
        }
        else if (bfs_ataque.dist == -9) {
            last_moves.push_back(id);
            moved_soldiers.insert(id);
            continue;
        }
      }

      // Prioridad 2: Buscamos dinero
      Option bfs_dinero = BFS_soldiers(soldier.pos,busco_dinero,40);
      if (bfs_dinero.dist != -1) {
        move(id,bfs_dinero.dir);
        moved_soldiers.insert(id);
        continue;
      }

      // Prioridad 3: Buscamos veneno
      Option bfs_veneno = BFS_soldiers(soldier.pos,busco_veneno,40);
      if (bfs_veneno.dist != -1) {
        move(id,bfs_veneno.dir);
        moved_soldiers.insert(id);
        continue;
      }

      // Prioridad 4: Buscamos kit's de salud
      // solo buscamos si mi profesor esta vivo o si está muerto, aparece antes de la ronda 185
      if (professor_is_valid()) {
        Option bfs_salud = BFS_soldiers(soldier.pos,busco_salud,40);
        if (bfs_salud.dist != -1) {
          move(id, bfs_salud.dir);
          moved_soldiers.insert(id);
          continue;
        }
      }

      // Última opción, movimiento aleatorio.
      Dir d = movimiento_aleatorio(soldier.pos);
      move(id,d);
      moved_soldiers.insert(id);
    }      

    for (int id : last_moves) {
        Unit u = unit(id);
        for (Dir d : Dirs) {
            Pos next = u.pos + d;
            if (is_valid(next) and has_enemy(next)) {
                move(id,d);
                break;
            }
        }
    }
  }


  // ===========================
  //    MOVIMIENTO PROFESOR
  // ===========================

  /**
   * Comprueba si en una tripleta un elemento es la suma de las otras dos
   */ 
  bool is_combination(int x,int y,int z) const {
    return (x + y == z) or (x + z == y) or (z + y == x);
  }

  /**
   * Función para resolver la contraseña de una caja fuerte
   */
  bool backtracking(const VI &hints,VI &solution,vector<Trip> &T, int i) {
    int M = hints.size();
    int n = T.size();
    if (i == M) return true;

    int hint = hints[i];
    for (int j = 0; j < n; ++j) {
      if (T[j].cont < 3) {
        bool compleix = true;
        // miramos si tenemos dos elementos, y si al añadir el tercero cumple la combinación
        if (T[j].cont == 2) {
          if (not is_combination(T[j].v[0],T[j].v[1],hint)) compleix = false;
        }
        // si cumple la combinación o tiene menos de dos elementos, insertamos el nuevo elemento
        if (compleix) {
          T[j].v[T[j].cont++] = hint;
          solution[i] = j;
          // Llamada recursiva
          if (backtracking(hints,solution,T,i + 1)) {
            return true;
          }
          // Backtracking
          --T[j].cont;
        }
      }
    }
    return false; // en teoría siempre hay una solución
  }

  /**
   * Función principal del profesor
   */
  void move_profesor() {
    int id = professor(me());
    Unit profesor = unit(id);
    Pos p = profesor.pos;
    bool moved = false;

    // Curarse si estoy <= 40 de vida
    if (profesor.health <= 40 and available_health(me()) >= 5) {
      heal(id);
      moved = true;
    }

    int id_box = -1;
    if (not moved) {
      // Prioridad 0 : Buscar y resolver las cajas fuertes
      pair<Dir,int> bfs_box = BFS_profesor(p,caja_fuerte,id_box,50);
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

    // Prioridad 1 : Busco dinero
    if (not moved) {
      pair<Dir,int> bfs_money = BFS_profesor(p,dinero,id_box,2);
      if (bfs_money.second > 0) {
        move(id,bfs_money.first);
        moved = true;
      }
    }

    // Prioridad 2: Busco veneno
    if (not moved) {
      pair<Dir,int> bfs_poison = BFS_profesor(p,kit_veneno,id_box,2);
      if (bfs_poison.second > 0) {
        move(id,bfs_poison.first);
        moved = true;
      }
    }

    // Prioridad 3: Busco kit's salud
    if (not moved) {
      pair<Dir,int> bfs_salud = BFS_profesor(p,kit_salud,id_box,2);
      if (bfs_salud.second > 0) {
        move(id, bfs_salud.first);
        moved = true;
      }
    }

    // Última opción = Movimiento aleatorio intentando evitar al enemigo
    if (not moved) {
      Dir d = movimiento_aleatorio_seguro(p);
      move(id,d);
    }
  }


  // ===========================
  //          MAIN
  // ===========================
  virtual void play() {
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