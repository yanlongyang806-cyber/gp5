import sys
import math
import sqlite3

import pygame

class Application(object):
    
    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((640, 480))
        self.clock = pygame.time.Clock()
        self.running = False
        self.geoip = GeoIP('geoip.db')
        self.map = Map(self.screen)
        self.add_pin('74.125.45.100')
        self.map.add_pin(40.735681, -73.99043)
        self.map.add_pin(38.895111, -77.036667)
        self.map.add_pin(48.856667, 2.350833)
    
    def __call__(self):
        self.running = True
        while self.running:
            delta = self.clock.tick(30)
            
            for evt in pygame.event.get():
                if evt.type == pygame.QUIT:
                    self.running = False
                elif evt.type == pygame.KEYDOWN:
                    if evt.key == pygame.K_ESCAPE:
                        self.running = False
                    elif evt.key == pygame.K_DOWN:
                        self.map.direction_y = 1
                    elif evt.key == pygame.K_UP:
                        self.map.direction_y = -1
                    elif evt.key == pygame.K_LEFT:
                        self.map.direction_x = -1
                    elif evt.key == pygame.K_RIGHT:
                        self.map.direction_x = 1
                    elif evt.key == pygame.K_a:
                        self.map.direction_z = -1
                    elif evt.key == pygame.K_s:
                        self.map.direction_z = 1
                elif evt.type == pygame.KEYUP:
                    if evt.key == pygame.K_DOWN:
                        self.map.direction_y = 0
                    elif evt.key == pygame.K_UP:
                        self.map.direction_y = 0
                    elif evt.key == pygame.K_LEFT:
                        self.map.direction_x = 0
                    elif evt.key == pygame.K_RIGHT:
                        self.map.direction_x = 0
                    elif evt.key == pygame.K_a:
                        self.map.direction_z = 0
                    elif evt.key == pygame.K_s:
                        self.map.direction_z = 0
            
            self.map.update(delta)
            
            self.screen.fill((255, 255, 255))
            self.map.draw(self.screen)
            pygame.display.flip()
    
    def add_pin(self, ip):
        lat, lon = self.geoip[ip]
        self.map.add_pin(lat, lon)


class Map(object):
    """A model of a mercator projection map."""
    
    #image_path = 'big_world_map.png'
    image_path = 'mercator_big2.png'
    
    def __init__(self, screen):
        self.source_image = pygame.image.load(self.image_path).convert()
        self.image = self.source_image
        self.rect = screen.get_rect()
        self.source_image_rect = self.image.get_rect()
        self.image_rect = self.source_image_rect.copy()
        self.speed = 1
        self.zoom = 0.3
        self.last_zoom = None
        self.zoom_speed = 0.001
        self.direction_x = self.direction_y = self.direction_z = 0 
        self.points = []
        self.coords = []
    
    def _to_point(self, lat, lon):
        x = self.image_rect.centerx + (lon * self.image_rect.width / 360.0) 
        lat_rad = math.radians(lat)
        radius = self.image_rect.height / (2*math.pi)
        y = math.log( (1.0 + math.sin(lat_rad)) /
                      (1.0 - math.sin(lat_rad)) ) * (radius/2)
        y = self.image_rect.centery - y
        return x, y
    
    def add_pin(self, lat, lon):
        p = self._to_point(lat, lon)
        self.points.append(pygame.Rect(p, (0 ,0)))
        self.coords.append((lat, lon))
    
    def update_pins(self):
        for i, (lat, lon) in enumerate(self.coords):
            p = self._to_point(lat, lon)
            self.points[i].topleft = p
    
    def update(self, delta):
        delta_x = self.direction_x * delta * self.speed
        delta_y = self.direction_y * delta * self.speed
        delta_z = self.direction_z * delta * self.zoom_speed
        self.rect.move_ip(delta_x, delta_y)
        self.rect.clamp_ip(self.image_rect)
        self.zoom += delta_z
        if self.zoom < 0.1:
            self.zoom = 0.1
        elif self.zoom > 1:
            self.zoom = 1
        if self.zoom != self.last_zoom:
            self.image = pygame.transform.smoothscale(self.source_image, (int(self.source_image_rect.width*self.zoom), int(self.source_image_rect.height*self.zoom)))
            self.image_rect = self.image.get_rect()
            self.update_pins()
            self.last_zoom = self.zoom
    
    def draw(self, screen):
        screen.blit(self.image, (0,0), self.rect)
        for p in self.points:
            pygame.draw.circle(screen, (255,0,0), p.move(self.rect.left*-1, self.rect.top*-1).topleft, 2)


class GeoIP(object):
    """A model for GeoIP lookups."""
    
    def __init__(self, db_file):
        self.db = sqlite3.connect(db_file)
        self.cursor = self.db.cursor()
    
    def __getitem__(self, key):
        a, b, c, d = key.split('.')
        #ip = ((A*256+B)*256+C)*256 + D
        ip = int(d)
        ip += int(c) * 256
        ip += int(b) * 256**2
        ip += int(a) * 256**3
        self.cursor.execute('SELECT latitude, longitude FROM ip_group_city where ip_start <= ? order by ip_start desc limit 1', (ip,))
        row = self.cursor.fetchone()
        if row is None:
            return 0, 0
        return float(row[0]), float(row[1])


if __name__ == '__main__':
    app = Application()
    app()
    #print app.geoip['74.125.45.100']
                            