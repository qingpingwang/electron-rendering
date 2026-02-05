#version 330 core

uniform sampler2D inputTexture;
uniform sampler2D mask;

uniform float time;
uniform vec2 direction;

in vec2 texCoord;
out vec4 FragColor;

float getFps() { return 12.0; }

float getIntensity() { return 0.0035*2.0; }

vec2 getDirection() {
  vec2 directions[5];
  directions[0] = vec2(-1.0, -1.0);
  directions[1] = vec2(-1.0, 1.0);
  directions[2] = vec2(1.0, 1.0);
  directions[3] = vec2(1.0, -1.0);
  directions[4] = vec2(0.0, 0.0);

  int id = int(floor(getFps() * time / 1000.0));
  id = id % 10 + 1;
  if (id > 5) {
    id = 10 - id;
    if (id == 0) {
      id = 5;
    }
  }
  return directions[id] * getIntensity();
}
void main()
{
    vec2 dt = getDirection();
    vec4 srcColor = texture(inputTexture, texCoord + dt);
    vec4 maskColor = texture(mask, texCoord);
    vec3 resultColor = mix(srcColor.rgb, maskColor.rgb, maskColor.a);
    FragColor = vec4(resultColor, 1.0);
}