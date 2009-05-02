/*
  pygame - Python Game Library
  Copyright (C) 2000-2001  Pete Shinners
  Copyright (C) 2006 Rene Dudfield

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  Pete Shinners
  pete@shinners.org
*/

/*
 *  image module for pygame
 */
#include "pygame.h"
#include "pgcompat.h"
#include "pygamedocs.h"
#include "pgopengl.h"

struct _module_state {
    int is_extended;
};

#if PY3
#define GETSTATE(m) PY3_GETSTATE (_module_state, m)
#else
static struct _module_state _state = { 0 };
#define GETSTATE(m) PY2_GETSTATE (_state)
#endif

static int SaveTGA (SDL_Surface *surface, char *file, int rle);
static int SaveTGA_RW (SDL_Surface *surface, SDL_RWops *out, int rle);
static SDL_Surface* opengltosdl (void);

#define DATAROW(data, row, width, height, flipped)               \
    ((flipped) ? (((char*) data) + (height - row - 1) * width) : \
     (((char*) data) + row * width))

static PyObject*
image_load_basic (PyObject* self, PyObject* arg)
{
    PyObject* file, *final;
    char* name = NULL;
    SDL_Surface* surf;
    SDL_RWops *rw;
    if (!PyArg_ParseTuple (arg, "O|s", &file, &name))
        return NULL;

#if PY3
    if (PyUnicode_Check (file)) {
        if (!PyArg_ParseTuple (arg, "s|O", &name, &file))
            return NULL;
        Py_BEGIN_ALLOW_THREADS;
        surf = SDL_LoadBMP (name);
        Py_END_ALLOW_THREADS;
    }
    else if (PyBytes_Check (file)) {
        if (!PyArg_ParseTuple (arg, "y|O", &name, &file))
            return NULL;
        Py_BEGIN_ALLOW_THREADS;
        surf = SDL_LoadBMP (name);
        Py_END_ALLOW_THREADS;
    }
#else
    if (PyString_Check (file) || PyUnicode_Check (file))
    {
        if (!PyArg_ParseTuple (arg, "s|O", &name, &file))
            return NULL;
        Py_BEGIN_ALLOW_THREADS;
        surf = SDL_LoadBMP (name);
        Py_END_ALLOW_THREADS;
    }
#endif
    else
    {
        if (!(rw = RWopsFromPython (file)))
            return NULL;
        if (RWopsCheckPython (rw))
            surf = SDL_LoadBMP_RW (rw, 1);
        else
        {
            Py_BEGIN_ALLOW_THREADS;
            surf = SDL_LoadBMP_RW (rw, 1);
            Py_END_ALLOW_THREADS;
        }
    }
    if (!surf)
        return RAISE (PyExc_SDLError, SDL_GetError ());

    final = PySurface_New (surf);
    if (!final)
        SDL_FreeSurface (surf);
    return final;
}


static SDL_Surface*
opengltosdl ()
{
    /*we need to get ahold of the pyopengl glReadPixels function*/
    /*we use pyopengl's so we don't need to link with opengl at compiletime*/
    SDL_Surface *surf = NULL;
    Uint32 rmask, gmask, bmask;
    int i;
    unsigned char *pixels = NULL;

    GL_glReadPixels_Func p_glReadPixels = NULL;

    p_glReadPixels = (GL_glReadPixels_Func) SDL_GL_GetProcAddress("glReadPixels"); 

    surf = SDL_GetVideoSurface ();

    if(!surf) {
        RAISE (PyExc_RuntimeError, "Cannot get video surface.");
        return NULL;
    }


    if(!p_glReadPixels) {
        RAISE (PyExc_RuntimeError, "Cannot find glReadPixels function.");
        return NULL;
    }

    /* 
    GL_UNSIGNED_BYTE = 5121
    GL_RGB = 6407
    */

    pixels = (unsigned char*) malloc(surf->w * surf->h * 3);

    if(!pixels) {
        RAISE (PyExc_MemoryError, "Cannot allocate enough memory for pixels.");
        return NULL;
    }
    //p_glReadPixels(0, 0, surf->w, surf->h, 6407, 5121, pixels);
    //glReadPixels(0, 0, surf->w, surf->h, 0x1907, 0x1401, pixels);
    p_glReadPixels(0, 0, surf->w, surf->h, 0x1907, 0x1401, pixels);

    if (SDL_BYTEORDER == SDL_LIL_ENDIAN) {
        rmask=0x000000FF;
        gmask=0x0000FF00;
        bmask=0x00FF0000;
    } else {
        rmask=0x00FF0000;
        gmask=0x0000FF00;
        bmask=0x000000FF;
    }
    surf = SDL_CreateRGBSurface (SDL_SWSURFACE, surf->w, surf->h, 24,
                                 rmask, gmask, bmask, 0);
    if (!surf) {
        free(pixels);
        RAISE (PyExc_SDLError, SDL_GetError ());
        return NULL;
    }

    for (i = 0; i < surf->h; ++i) {
        memcpy (((char *) surf->pixels) + surf->pitch * i,
                pixels + 3 * surf->w * (surf->h - i - 1), surf->w * 3);
    }


    free(pixels);
    return surf;
}

PyObject*
image_save (PyObject* self, PyObject* arg)
{
    PyObject* surfobj, *file;
    PyObject *imgext = NULL;
    SDL_Surface *surf;
    SDL_Surface *temp = NULL;
    int result = 0;

    if (!PyArg_ParseTuple (arg, "O!O", &PySurface_Type, &surfobj, &file))
        return NULL;
    surf = PySurface_AsSurface (surfobj);

    if (surf->flags & SDL_OPENGL)
    {
        temp = surf = opengltosdl ();
        if (!surf)
            return NULL;
    }
    else
        PySurface_Prep (surfobj);

#if PY3
    if (PyBytes_Check (file) || PyUnicode_Check (file))
#else
    if (PyString_Check (file) || PyUnicode_Check (file))
#endif
    {
        int namelen;
        char* name;
        int written = 0;

#if PY3
        if (PyUnicode_Check (file)) {
            if (!PyArg_ParseTuple (arg, "O|s", &file, &name)) {
                return NULL;
            }
        }
        else {
            if (!PyArg_ParseTuple (arg, "O|y", &file, &name)) {
                return NULL;
            }
        }
#else
        if (!PyArg_ParseTuple (arg, "O|s", &file, &name))
            return NULL;
#endif
        namelen = strlen (name);
        if (namelen > 3)
        {
            if ((name[namelen - 1]=='p' || name[namelen - 1]=='P') &&
                (name[namelen - 2]=='m' || name[namelen - 2]=='M') &&
                (name[namelen - 3]=='b' || name[namelen - 3]=='B'))
            {
                Py_BEGIN_ALLOW_THREADS;
                result = SDL_SaveBMP (surf, name);
                Py_END_ALLOW_THREADS;
                written = 1;
            }
            else if (((name[namelen - 1]=='g' || name[namelen - 1]=='G') &&
                    (name[namelen - 2]=='n' || name[namelen - 2]=='N') &&
                    (name[namelen - 3]=='p' || name[namelen - 3]=='P')) ||
                ((name[namelen - 1]=='g' || name[namelen - 1]=='G') &&
                    (name[namelen - 2]=='e' || name[namelen - 2]=='E') &&
                    (name[namelen - 3]=='p' || name[namelen - 3]=='P') &&
                    (name[namelen - 4]=='j' || name[namelen - 4]=='J')) ||
                ((name[namelen - 1]=='g' || name[namelen - 1]=='G') &&
                    (name[namelen - 2]=='p' || name[namelen - 2]=='P') &&
                    (name[namelen - 3]=='j' || name[namelen - 3]=='J')))
            {
                /* If it is .png .jpg .jpeg use the extended module. */
                /* try to get extended formats */
                imgext = PyImport_ImportModule ("pygame.imageext");
                if (imgext)
                {
                    PyObject *extdict = PyModule_GetDict (imgext);
                    PyObject* extsave = PyDict_GetItemString (extdict,
                        "save_extended");
                    PyObject* data = PyObject_CallObject (extsave, arg);
                    if (!data)
                        result = -1;
                    Py_DECREF (imgext);
                    /* Data must be decremented here, not? */
                    if (data)
		    {
                    	Py_DECREF (data);
		    }
                }
                else
		{
                    result = -2;
		}
                written = 1;
            }
        }

        if (!written)
        {
            Py_BEGIN_ALLOW_THREADS;
            result = SaveTGA (surf, name, 1);
            Py_END_ALLOW_THREADS;
        }
    }
    else
    {
        SDL_RWops* rw;
        if (!(rw = RWopsFromPython (file)))
            return NULL;
        result = SaveTGA_RW (surf, rw, 1);
    }

    if (temp)
        SDL_FreeSurface (temp);
    else
        PySurface_Unprep (surfobj);
        
    if (result == -2)
        return imgext;
    if (result == -1)
        return RAISE (PyExc_SDLError, SDL_GetError ());

    Py_RETURN_NONE;
}


PyObject*
image_get_extended (PyObject* self, PyObject* arg)
{
    return PyInt_FromLong (GETSTATE (self)->is_extended);
}

PyObject*
image_tostring (PyObject* self, PyObject* arg)
{
    PyObject *surfobj, *string = NULL;
    char *format, *data, *pixels;
    SDL_Surface *surf, *temp = NULL;
    int w, h, color, flipped = 0;
    Py_ssize_t len;
    Uint32 Rmask, Gmask, Bmask, Amask, Rshift, Gshift, Bshift, Ashift, Rloss,
        Gloss, Bloss, Aloss;
    int hascolorkey, colorkey;
    Uint32 alpha;

    if (!PyArg_ParseTuple (arg, "O!s|i", &PySurface_Type, &surfobj, &format,
                           &flipped))
        return NULL;
    surf = PySurface_AsSurface (surfobj);
    if (surf->flags & SDL_OPENGL)
    {
        temp = surf = opengltosdl ();
        if (!surf)
            return NULL;
    }

    Rmask = surf->format->Rmask;
    Gmask = surf->format->Gmask;
    Bmask = surf->format->Bmask;
    Amask = surf->format->Amask;
    Rshift = surf->format->Rshift;
    Gshift = surf->format->Gshift;
    Bshift = surf->format->Bshift;
    Ashift = surf->format->Ashift;
    Rloss = surf->format->Rloss;
    Gloss = surf->format->Gloss;
    Bloss = surf->format->Bloss;
    Aloss = surf->format->Aloss;
    hascolorkey = (surf->flags & SDL_SRCCOLORKEY) && !Amask;
    colorkey = surf->format->colorkey;

    if (!strcmp (format, "P"))
    {
        if (surf->format->BytesPerPixel != 1)
            return RAISE
                (PyExc_ValueError,
                 "Can only create \"P\" format data with 8bit Surfaces");
        string = Bytes_FromStringAndSize (NULL, surf->w * surf->h);
        if (!string)
            return NULL;
        Bytes_AsStringAndSize (string, &data, &len);

        PySurface_Lock (surfobj);
        pixels = (char*) surf->pixels;
        for (h = 0; h < surf->h; ++h)
            memcpy (DATAROW (data, h, surf->w, surf->h, flipped),
                    pixels + (h * surf->pitch), surf->w);
        PySurface_Unlock (surfobj);
    }
    else if (!strcmp (format, "RGB"))
    {
        string = Bytes_FromStringAndSize (NULL, surf->w * surf->h * 3);
        if (!string)
            return NULL;
        Bytes_AsStringAndSize (string, &data, &len);

        if (!temp)
            PySurface_Lock (surfobj);
        pixels = (char*) surf->pixels;
        switch (surf->format->BytesPerPixel)
        {
        case 1:
            for (h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[0] = (char) surf->format->palette->colors[color].r;
                    data[1] = (char) surf->format->palette->colors[color].g;
                    data[2] = (char) surf->format->palette->colors[color].b;
                    data += 3;
                }
            }
            break;
        case 2:
            for (h = 0; h < surf->h; ++h)
            {
                Uint16* ptr = (Uint16*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[0] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[1] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[2] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data += 3;
                }
            }
            break;
        case 3:
            for (h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                    color = ptr[0] + (ptr[1]<<8) + (ptr[2]<<16);
#else
                    color = ptr[2] + (ptr[1]<<8) + (ptr[0]<<16);
#endif
                    ptr += 3;
                    data[0] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[1] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[2] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data += 3;
                }
            }
            break;
        case 4:
            for (h = 0; h < surf->h; ++h)
            {
                Uint32* ptr = (Uint32*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[0] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[1] = (char) (((color & Gmask) >> Gshift) << Rloss);
                    data[2] = (char) (((color & Bmask) >> Bshift) << Rloss);
                    data += 3;
                }
            }
            break;
        }
        if (!temp)
            PySurface_Unlock (surfobj);
    }
    else if (!strcmp (format, "RGBX") || !strcmp (format, "RGBA"))
    {
        if (strcmp (format, "RGBA"))
            hascolorkey = 0;

        string = Bytes_FromStringAndSize (NULL, surf->w * surf->h * 4);
        if (!string)
            return NULL;
        Bytes_AsStringAndSize (string, &data, &len);

        PySurface_Lock (surfobj);
        pixels = (char*) surf->pixels;
        switch (surf->format->BytesPerPixel)
        {
        case 1:
            for(h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[0] = (char) surf->format->palette->colors[color].r;
                    data[1] = (char) surf->format->palette->colors[color].g;
                    data[2] = (char) surf->format->palette->colors[color].b;
                    data[3] = hascolorkey ? (char)(color != colorkey) * 255 :
                        (char)255;
                    data += 4;
                }
            }
            break;
        case 2:
            for (h = 0; h < surf->h; ++h)
            {
                Uint16* ptr = (Uint16*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[0] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[1] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[2] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data[3] = hascolorkey ? (char)(color!=colorkey) * 255 :
                        (char)(Amask ? (((color & Amask) >> Ashift) << Aloss) :
                               255);
                    data += 4;
                }
            }
            break;
        case 3:
            for (h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                    color = ptr[0] + (ptr[1]<<8) + (ptr[2]<<16);
#else
                    color = ptr[2] + (ptr[1]<<8) + (ptr[0]<<16);
#endif
                    ptr += 3;
                    data[0] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[1] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[2] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data[3] = hascolorkey ? (char)(color!=colorkey)*255 :
                        (char)(Amask ? (((color & Amask) >> Ashift) << Aloss) :
                               255);
                    data += 4;
                }
            }
            break;
        case 4:
            for (h = 0; h < surf->h; ++h)
            {
                Uint32* ptr = (Uint32*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[0] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[1] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[2] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data[3] = hascolorkey ? (char)(color!=colorkey)*255 :
                        (char)(Amask ? (((color & Amask) >> Ashift) << Aloss) :
                               255);
                    data += 4;
                }
            }
            break;
        }
        PySurface_Unlock (surfobj);
    }
    else if (!strcmp (format, "ARGB"))
    {
        hascolorkey = 0;

        string = Bytes_FromStringAndSize (NULL, surf->w * surf->h * 4);
        if (!string)
            return NULL;
        Bytes_AsStringAndSize (string, &data, &len);

        PySurface_Lock (surfobj);
        pixels = (char*) surf->pixels;
        switch (surf->format->BytesPerPixel)
        {
        case 1:
            for (h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[1] = (char)surf->format->palette->colors[color].r;
                    data[2] = (char)surf->format->palette->colors[color].g;
                    data[3] = (char)surf->format->palette->colors[color].b;
                    data[0] = hascolorkey ? (char)(color!=colorkey)*255 :
                        (char)255;
                    data += 4;
                }
            }
            break;
        case 2:
            for(h = 0; h < surf->h; ++h)
            {
                Uint16* ptr = (Uint16*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[1] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[2] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[3] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data[0] = hascolorkey ? (char)(color!=colorkey)*255 :
                        (char)(Amask ? (((color & Amask) >> Ashift) << Aloss) :
                               255);
                    data += 4;
                }
            }
            break;
        case 3:
            for (h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                    color = ptr[0] + (ptr[1]<<8) + (ptr[2]<<16);
#else
                    color = ptr[2] + (ptr[1]<<8) + (ptr[0]<<16);
#endif
                    ptr += 3;
                    data[1] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[2] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[3] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data[0] = hascolorkey ? (char)(color!=colorkey)*255 :
                        (char)(Amask ? (((color & Amask) >> Ashift) << Aloss) :
                               255);
                    data += 4;
                }
            }
            break;
        case 4:
            for (h = 0; h < surf->h; ++h)
            {
                Uint32* ptr = (Uint32*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    data[1] = (char) (((color & Rmask) >> Rshift) << Rloss);
                    data[2] = (char) (((color & Gmask) >> Gshift) << Gloss);
                    data[3] = (char) (((color & Bmask) >> Bshift) << Bloss);
                    data[0] = hascolorkey ? (char)(color!=colorkey)*255 :
                        (char)(Amask ? (((color & Amask) >> Ashift) << Aloss) :
                               255);
                    data += 4;
                }
            }
            break;
        }
        PySurface_Unlock (surfobj);
    }
    else if (!strcmp (format, "RGBA_PREMULT"))
    {
        if (surf->format->BytesPerPixel == 1 || surf->format->Amask == 0)
            return RAISE
                (PyExc_ValueError,
                 "Can only create pre-multiplied alpha strings if the surface has per-pixel alpha");

        hascolorkey = 0;

        string = Bytes_FromStringAndSize (NULL, surf->w * surf->h * 4);
        if (!string)
            return NULL;
        Bytes_AsStringAndSize (string, &data, &len);

        PySurface_Lock (surfobj);
        pixels = (char*) surf->pixels;
        switch (surf->format->BytesPerPixel)
        {
        case 2:
            for (h = 0; h < surf->h; ++h)
            {
                Uint16* ptr = (Uint16*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    alpha = ((color & Amask) >> Ashift) << Aloss;
                    data[0] = (char) ((((color & Rmask) >> Rshift) << Rloss)*alpha/255);
                    data[1] = (char) ((((color & Gmask) >> Gshift) << Gloss)*alpha/255);
                    data[2] = (char) ((((color & Bmask) >> Bshift) << Bloss)*alpha/255);
                    data[3] = (char) alpha;
                    data += 4;
                }
            }
            break;
        case 3:
            for (h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                    color = ptr[0] + (ptr[1]<<8) + (ptr[2]<<16);
#else
                    color = ptr[2] + (ptr[1]<<8) + (ptr[0]<<16);
#endif
                    ptr += 3;
                    alpha = ((color & Amask) >> Ashift) << Aloss;
                    data[0] = (char) ((((color & Rmask) >> Rshift) << Rloss)*alpha/255);
                    data[1] = (char) ((((color & Gmask) >> Gshift) << Gloss)*alpha/255);
                    data[2] = (char) ((((color & Bmask) >> Bshift) << Bloss)*alpha/255);
                    data[3] = (char) alpha;
                    data += 4;
                }
            }
            break;
        case 4:
            for (h = 0; h < surf->h; ++h)
            {
                Uint32* ptr = (Uint32*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    alpha = ((color & Amask) >> Ashift) << Aloss;
                    if (alpha == 0)
                    {
                        data[0] = data[1] = data[2] = 0;
                    }
                    else
                    {
                        data[0] = (char) ((((color & Rmask) >> Rshift) << Rloss)*alpha/255);
                        data[1] = (char) ((((color & Gmask) >> Gshift) << Gloss)*alpha/255);
                        data[2] = (char) ((((color & Bmask) >> Bshift) << Bloss)*alpha/255);                    
                    }
                    data[3] = (char) alpha;
                    data += 4;
                }
            }
            break;
        }
        PySurface_Unlock (surfobj);
    }
    else if (!strcmp (format, "ARGB_PREMULT"))
    {
        if (surf->format->BytesPerPixel == 1 || surf->format->Amask == 0)
            return RAISE
                (PyExc_ValueError,
                 "Can only create pre-multiplied alpha strings if the surface has per-pixel alpha");

        hascolorkey = 0;

        string = Bytes_FromStringAndSize (NULL, surf->w * surf->h * 4);
        if (!string)
            return NULL;
        Bytes_AsStringAndSize (string, &data, &len);

        PySurface_Lock (surfobj);
        pixels = (char*) surf->pixels;
        switch (surf->format->BytesPerPixel)
        {
        case 2:
            for (h = 0; h < surf->h; ++h)
            {
                Uint16* ptr = (Uint16*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    alpha = ((color & Amask) >> Ashift) << Aloss;
                    data[1] = (char) ((((color & Rmask) >> Rshift) << Rloss)*alpha/255);
                    data[2] = (char) ((((color & Gmask) >> Gshift) << Gloss)*alpha/255);
                    data[3] = (char) ((((color & Bmask) >> Bshift) << Bloss)*alpha/255);
                    data[0] = (char) alpha;
                    data += 4;
                }
            }
            break;
        case 3:
            for (h = 0; h < surf->h; ++h)
            {
                Uint8* ptr = (Uint8*) DATAROW (surf->pixels, h, surf->pitch,
                                               surf->h, flipped);
                for (w = 0; w < surf->w; ++w)
                {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                    color = ptr[0] + (ptr[1]<<8) + (ptr[2]<<16);
#else
                    color = ptr[2] + (ptr[1]<<8) + (ptr[0]<<16);
#endif
                    ptr += 3;
                    alpha = ((color & Amask) >> Ashift) << Aloss;
                    data[1] = (char) ((((color & Rmask) >> Rshift) << Rloss)*alpha/255);
                    data[2] = (char) ((((color & Gmask) >> Gshift) << Gloss)*alpha/255);
                    data[3] = (char) ((((color & Bmask) >> Bshift) << Bloss)*alpha/255);
                    data[0] = (char) alpha;
                    data += 4;
                }
            }
            break;
        case 4:
            for (h = 0; h < surf->h; ++h)
            {
                Uint32* ptr = (Uint32*) DATAROW (surf->pixels, h, surf->pitch,
                                                 surf->h, flipped);
                for(w = 0; w < surf->w; ++w)
                {
                    color = *ptr++;
                    alpha = ((color & Amask) >> Ashift) << Aloss;
                    if (alpha == 0)
                    {
                        data[1] = data[2] = data[3] = 0;
                    }
                    else
                    {
                        data[1] = (char) ((((color & Rmask) >> Rshift) << Rloss)*alpha/255);
                        data[2] = (char) ((((color & Gmask) >> Gshift) << Gloss)*alpha/255);
                        data[3] = (char) ((((color & Bmask) >> Bshift) << Bloss)*alpha/255);                    
                    }
                    data[0] = (char) alpha;
                    data += 4;
                }
            }
            break;
        }
        PySurface_Unlock (surfobj);
    }
    else
    {
        if (temp)
            SDL_FreeSurface (temp);
        return RAISE (PyExc_ValueError, "Unrecognized type of format");
    }

    if (temp)
        SDL_FreeSurface (temp);
    return string;
}

PyObject*
image_fromstring (PyObject* self, PyObject* arg)
{
    PyObject *string;
    char *format, *data;
    SDL_Surface *surf = NULL;
    int w, h, flipped=0;
    Py_ssize_t len;
    int loopw, looph;

    if (!PyArg_ParseTuple (arg, "O!(ii)s|i", &Bytes_Type, &string, &w, &h,
                           &format, &flipped))
        return NULL;

    if (w < 1 || h < 1)
        return RAISE (PyExc_ValueError, "Resolution must be positive values");

    Bytes_AsStringAndSize (string, &data, &len);

    if (!strcmp (format, "P"))
    {
        if (len != w * h)
            return RAISE
                (PyExc_ValueError,
                 "String length does not equal format and resolution size");

        surf = SDL_CreateRGBSurface (0, w, h, 8, 0, 0, 0, 0);
        if (!surf)
            return RAISE (PyExc_SDLError, SDL_GetError ());
        SDL_LockSurface (surf);
        for (looph = 0; looph < h; ++looph)
            memcpy(((char*) surf->pixels) + looph * surf->pitch,
                   DATAROW (data, looph, w, h, flipped), w);
        SDL_UnlockSurface (surf);
    }
    else if (!strcmp (format, "RGB"))
    {
        if (len != w * h * 3)
            return RAISE
                (PyExc_ValueError,
                 "String length does not equal format and resolution size");
        surf = SDL_CreateRGBSurface (0, w, h, 24, 0xFF<<16, 0xFF<<8, 0xFF, 0);
        if (!surf)
            return RAISE (PyExc_SDLError, SDL_GetError ());
        SDL_LockSurface (surf);
        for (looph = 0; looph < h; ++looph)
        {
            Uint8* pix = (Uint8*) DATAROW (surf->pixels, looph, surf->pitch, h,
                                           flipped);
            for (loopw = 0; loopw < w; ++loopw)
            {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                pix[2] = data[0];
                pix[1] = data[1];
                pix[0] = data[2];
#else
                pix[0] = data[0];
                pix[1] = data[1];
                pix[2] = data[2];
#endif
                pix += 3;
                data += 3;
            }
        }
        SDL_UnlockSurface (surf);
    }
    else if (!strcmp (format, "RGBA") || !strcmp (format, "RGBX"))
    {
        int alphamult = !strcmp (format, "RGBA");
        if (len != w * h * 4)
            return RAISE
                (PyExc_ValueError,
                 "String length does not equal format and resolution size");
        surf = SDL_CreateRGBSurface ((alphamult ? SDL_SRCALPHA : 0), w, h, 32,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                                     0xFF, 0xFF<<8, 0xFF<<16,
                                     (alphamult ? 0xFF << 24 : 0));
#else
                                     0xFF<<24, 0xFF<<16, 0xFF<<8,
                                     (alphamult ? 0xFF : 0));
#endif
        if (!surf)
            return RAISE (PyExc_SDLError, SDL_GetError ());
        SDL_LockSurface (surf);
        for (looph = 0; looph < h; ++looph)
        {
            Uint32* pix = (Uint32*) DATAROW (surf->pixels, looph, surf->pitch,
                                             h, flipped);
            for (loopw = 0; loopw < w; ++loopw)
            {
                *pix++ = *((Uint32*) data);
                data += 4;
            }
        }
        SDL_UnlockSurface (surf);
    }
    else if (!strcmp (format, "ARGB"))
    {
        if (len != w * h * 4)
            return RAISE
                (PyExc_ValueError,
                 "String length does not equal format and resolution size");
        surf = SDL_CreateRGBSurface (SDL_SRCALPHA, w, h, 32,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                                     0xFF<<8, 0xFF<<16, 0xFF<<24, 0xFF);
#else
                                     0xFF<<16, 0xFF<<8, 0xFF, 0xFF<<24);
#endif
        if (!surf)
            return RAISE (PyExc_SDLError, SDL_GetError ());
        SDL_LockSurface (surf);
        for (looph = 0; looph < h; ++looph)
        {
            Uint32* pix = (Uint32*) DATAROW (surf->pixels, looph, surf->pitch,
                                             h, flipped);
            for (loopw = 0; loopw < w; ++loopw)
            {
                *pix++ = *((Uint32*) data);
                data += 4;
            }
        }
        SDL_UnlockSurface (surf);
    }
    else
        return RAISE (PyExc_ValueError, "Unrecognized type of format");

    if (!surf)
        return NULL;
    return PySurface_New (surf);
}

PyObject*
image_frombuffer (PyObject* self, PyObject* arg)
{
    PyObject *buffer;
    char *format, *data;
    SDL_Surface *surf = NULL;
    int w, h;
    Py_ssize_t len;
    PyObject *surfobj;

    if (!PyArg_ParseTuple (arg, "O(ii)s|i", &buffer, &w, &h, &format))
        return NULL;

    if (w < 1 || h < 1)
        return RAISE (PyExc_ValueError, "Resolution must be positive values");

    /* breaking constness here, we should really not change this string */
    if (PyObject_AsCharBuffer (buffer, (const char**)&data, &len) == -1)
        return NULL;
        
    if (!strcmp (format, "P"))
    {
        if (len != w * h)
            return RAISE
                (PyExc_ValueError,
                 "Buffer length does not equal format and resolution size");

        surf = SDL_CreateRGBSurfaceFrom (data, w, h, 8, w, 0, 0, 0, 0);
    }
    else if (!strcmp (format, "RGB"))
    {
        if (len != w * h * 3)
            return RAISE
                (PyExc_ValueError,
                 "Buffer length does not equal format and resolution size");
        surf = SDL_CreateRGBSurfaceFrom (data, w, h, 24, w * 3,
                                         0xFF, 0xFF<<8, 0xFF<<16, 0);
    }
    else if (!strcmp (format, "RGBA") || !strcmp (format, "RGBX"))
    {
        int alphamult = !strcmp (format, "RGBA");
        if (len != w * h * 4)
            return RAISE
                (PyExc_ValueError,
                 "Buffer length does not equal format and resolution size");
        surf = SDL_CreateRGBSurfaceFrom (data, w, h, 32, w * 4,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                                         0xFF, 0xFF<<8, 0xFF<<16,
                                         (alphamult ? 0xFF << 24 : 0));
#else
                                         0xFF<<24, 0xFF<<16, 0xFF<<8,
                                         (alphamult ? 0xFF : 0));
#endif
        if(alphamult)
            surf->flags |= SDL_SRCALPHA;
    }
    else if (!strcmp (format, "ARGB"))
    {
        if (len != w * h * 4)
            return RAISE
                (PyExc_ValueError,
                 "Buffer length does not equal format and resolution size");
        surf = SDL_CreateRGBSurfaceFrom (data, w, h, 32, w*4,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                                         0xFF<<24, 0xFF, 0xFF<<8, 0xFF<<16);
#else
                                         0xFF, 0xFF<<24, 0xFF<<16, 0xFF<<8);
#endif
        surf->flags |= SDL_SRCALPHA;
    }
    else
        return RAISE(PyExc_ValueError, "Unrecognized type of format");

    if (!surf)
        return RAISE (PyExc_SDLError, SDL_GetError ());
    surfobj = PySurface_New (surf);
    Py_INCREF (buffer);
    ((PySurfaceObject*) surfobj)->dependency = buffer;
    return surfobj;
}

/*******************************************************/
/* tga code by Mattias Engdeg�rd, in the public domain */
/*******************************************************/
struct TGAheader
{
    Uint8 infolen;		/* length of info field */
    Uint8 has_cmap;		/* 1 if image has colormap, 0 otherwise */
    Uint8 type;

    Uint8 cmap_start[2];	/* index of first colormap entry */
    Uint8 cmap_len[2];		/* number of entries in colormap */
    Uint8 cmap_bits;		/* bits per colormap entry */

    Uint8 yorigin[2];		/* image origin (ignored here) */
    Uint8 xorigin[2];
    Uint8 width[2];		/* image size */
    Uint8 height[2];
    Uint8 pixel_bits;		/* bits/pixel */
    Uint8 flags;
};

enum tga_type
{
    TGA_TYPE_INDEXED = 1,
    TGA_TYPE_RGB = 2,
    TGA_TYPE_BW = 3,
    TGA_TYPE_RLE = 8		/* additive */
};

#define TGA_INTERLEAVE_MASK	0xc0
#define TGA_INTERLEAVE_NONE	0x00
#define TGA_INTERLEAVE_2WAY	0x40
#define TGA_INTERLEAVE_4WAY	0x80

#define TGA_ORIGIN_MASK		0x30
#define TGA_ORIGIN_LEFT		0x00
#define TGA_ORIGIN_RIGHT	0x10
#define TGA_ORIGIN_LOWER	0x00
#define TGA_ORIGIN_UPPER	0x20

/* read/write unaligned little-endian 16-bit ints */
#define LE16(p) ((p)[0] + ((p)[1] << 8))
#define SETLE16(p, v) ((p)[0] = (v), (p)[1] = (v) >> 8)

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define TGA_RLE_MAX 128		/* max length of a TGA RLE chunk */
/* return the number of bytes in the resulting buffer after RLE-encoding
   a line of TGA data */
static int
rle_line (Uint8 *src, Uint8 *dst, int w, int bpp)
{
    int x = 0;
    int out = 0;
    int raw = 0;
    while (x < w)
    {
	Uint32 pix;
	int x0 = x;
	memcpy (&pix, src + x * bpp, bpp);
	x++;
	while (x < w && memcmp (&pix, src + x * bpp, bpp) == 0
               && x - x0 < TGA_RLE_MAX)
	    x++;
	/* use a repetition chunk iff the repeated pixels would consume
	   two bytes or more */
	if ((x - x0 - 1) * bpp >= 2 || x == w)
        {
	    /* output previous raw chunks */
	    while (raw < x0)
            {
		int n = MIN (TGA_RLE_MAX, x0 - raw);
		dst[out++] = n - 1;
		memcpy (dst + out, src + raw * bpp, n * bpp);
		out += n * bpp;
		raw += n;
	    }

	    if (x - x0 > 0)
            {
		/* output new repetition chunk */
		dst[out++] = 0x7f + x - x0;
		memcpy (dst + out, &pix, bpp);
		out += bpp;
	    }
	    raw = x;
	}
    }
    return out;
}

/*
 * Save a surface to an output stream in TGA format.
 * 8bpp surfaces are saved as indexed images with 24bpp palette, or with
 *     32bpp palette if colourkeying is used.
 * 15, 16, 24 and 32bpp surfaces are saved as 24bpp RGB images,
 * or as 32bpp RGBA images if alpha channel is used.
 *
 * RLE compression is not used in the output file.
 *
 * Returns -1 upon error, 0 if success
 */
static int
SaveTGA_RW (SDL_Surface *surface, SDL_RWops *out, int rle)
{
    SDL_Surface *linebuf = NULL;
    int alpha = 0;
    int ckey = -1;
    struct TGAheader h;
    int srcbpp;
    unsigned surf_flags;
    unsigned surf_alpha;
    Uint32 rmask, gmask, bmask, amask;
    SDL_Rect r;
    int bpp;
    Uint8 *rlebuf = NULL;

    h.infolen = 0;
    SETLE16 (h.cmap_start, 0);

    srcbpp = surface->format->BitsPerPixel;
    if (srcbpp < 8)
    {
	SDL_SetError ("cannot save <8bpp images as TGA");
	return -1;
    }

    if (srcbpp == 8)
    {
	h.has_cmap = 1;
	h.type = TGA_TYPE_INDEXED;
	if (surface->flags & SDL_SRCCOLORKEY)
        {
	    ckey = surface->format->colorkey;
	    h.cmap_bits = 32;
	}
        else
	    h.cmap_bits = 24;
	SETLE16 (h.cmap_len, surface->format->palette->ncolors);
	h.pixel_bits = 8;
	rmask = gmask = bmask = amask = 0;
    }
    else
    {
	h.has_cmap = 0;
	h.type = TGA_TYPE_RGB;
	h.cmap_bits = 0;
	SETLE16 (h.cmap_len, 0);
	if (surface->format->Amask)
        {
	    alpha = 1;
	    h.pixel_bits = 32;
	}
        else
	    h.pixel_bits = 24;
	if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
        {
	    int s = alpha ? 0 : 8;
	    amask = 0x000000ff >> s;
	    rmask = 0x0000ff00 >> s;
	    gmask = 0x00ff0000 >> s;
	    bmask = 0xff000000 >> s;
	}
        else
        {
	    amask = alpha ? 0xff000000 : 0;
	    rmask = 0x00ff0000;
	    gmask = 0x0000ff00;
	    bmask = 0x000000ff;
	}
    }
    bpp = h.pixel_bits >> 3;
    if (rle)
        h.type += TGA_TYPE_RLE;

    SETLE16 (h.yorigin, 0);
    SETLE16 (h.xorigin, 0);
    SETLE16 (h.width, surface->w);
    SETLE16 (h.height, surface->h);
    h.flags = TGA_ORIGIN_UPPER | (alpha ? 8 : 0);

    if (!SDL_RWwrite (out, &h, sizeof (h), 1))
	return -1;

    if (h.has_cmap)
    {
	int i;
	SDL_Palette *pal = surface->format->palette;
	Uint8 entry[4];
	for (i = 0; i < pal->ncolors; i++)
        {
	    entry[0] = pal->colors[i].b;
	    entry[1] = pal->colors[i].g;
	    entry[2] = pal->colors[i].r;
	    entry[3] = (i == ckey) ? 0 : 0xff;
	    if (!SDL_RWwrite (out, entry, h.cmap_bits >> 3, 1))
		return -1;
	}
    }

    linebuf = SDL_CreateRGBSurface (SDL_SWSURFACE, surface->w, 1, h.pixel_bits,
                                    rmask, gmask, bmask, amask);
    if (!linebuf)
	return -1;
    if (h.has_cmap)
	SDL_SetColors (linebuf, surface->format->palette->colors, 0,
                       surface->format->palette->ncolors);
    if (rle)
    {
	rlebuf = malloc (bpp * surface->w + 1 + surface->w / TGA_RLE_MAX);
	if (!rlebuf)
        {
	    SDL_SetError ("out of memory");
	    goto error;
	}
    }

    /* Temporarily remove colourkey and alpha from surface so copies are
       opaque */
    surf_flags = surface->flags & (SDL_SRCALPHA | SDL_SRCCOLORKEY);
    surf_alpha = surface->format->alpha;
    if (surf_flags & SDL_SRCALPHA)
	SDL_SetAlpha (surface, 0, 255);
    if (surf_flags & SDL_SRCCOLORKEY)
	SDL_SetColorKey (surface, 0, surface->format->colorkey);

    r.x = 0;
    r.w = surface->w;
    r.h = 1;
    for (r.y = 0; r.y < surface->h; r.y++)
    {
	int n;
	void *buf;
	if (SDL_BlitSurface (surface, &r, linebuf, NULL) < 0)
	    break;
	if (rle)
        {
	    buf = rlebuf;
	    n = rle_line (linebuf->pixels, rlebuf, surface->w, bpp);
	}
        else
        {
	    buf = linebuf->pixels;
	    n = surface->w * bpp;
	}
	if (!SDL_RWwrite (out, buf, n, 1))
	    break;
    }

    /* restore flags */
    if (surf_flags & SDL_SRCALPHA)
	SDL_SetAlpha (surface, SDL_SRCALPHA, (Uint8)surf_alpha);
    if (surf_flags & SDL_SRCCOLORKEY)
	SDL_SetColorKey (surface, SDL_SRCCOLORKEY, surface->format->colorkey);

error:
    free (rlebuf);
    SDL_FreeSurface (linebuf);
    return 0;
}

static int
SaveTGA (SDL_Surface *surface, char *file, int rle)
{
    SDL_RWops *out = SDL_RWFromFile (file, "wb");
    int ret;
    if (!out)
	return -1;
    ret = SaveTGA_RW (surface, out, rle);
    SDL_RWclose (out);
    return ret;
}

static PyMethodDef _image_methods[] =
{
    { "load_basic", image_load_basic, METH_VARARGS, DOC_PYGAMEIMAGELOAD },
    { "save", image_save, METH_VARARGS, DOC_PYGAMEIMAGESAVE },
    { "get_extended", (PyCFunction) image_get_extended, METH_NOARGS,
      DOC_PYGAMEIMAGEGETEXTENDED },

    { "tostring", image_tostring, METH_VARARGS, DOC_PYGAMEIMAGETOSTRING },
    { "fromstring", image_fromstring, METH_VARARGS, DOC_PYGAMEIMAGEFROMSTRING },
    { "frombuffer", image_frombuffer, METH_VARARGS, DOC_PYGAMEIMAGEFROMBUFFER },

    { NULL, NULL, 0, NULL }
};


MODINIT_DEFINE (image)
{
    PyObject *module, *dict;
    PyObject *extmodule;
    struct _module_state *st;

#if PY3
    static struct PyModuleDef _module = {
        PyModuleDef_HEAD_INIT,
        "image",
        DOC_PYGAMEIMAGE,
        sizeof (struct _module_state),
        _image_methods,
        NULL, NULL, NULL, NULL
    };
#endif

    /* imported needed apis; Do this first so if there is an error
       the module is not loaded.
    */
    import_pygame_base ();
    if (PyErr_Occurred ()) {
	MODINIT_ERROR;;
    }
    import_pygame_surface ();
    if (PyErr_Occurred ()) {
	MODINIT_ERROR;;
    }
    import_pygame_rwobject ();
    if (PyErr_Occurred ()) {
	MODINIT_ERROR;;
    }

    /* create the module */
#if PY3
    module = PyModule_Create (&_module);
#else
    module = Py_InitModule3 ("image", _image_methods, DOC_PYGAMEIMAGE);
#endif
    if (module == NULL) {
        MODINIT_ERROR;
    }
    st = GETSTATE (module);
    dict = PyModule_GetDict (module);


    /* try to get extended formats */
    extmodule = PyImport_ImportModule ("pygame.imageext");
    if (extmodule)
    {
        PyObject *extdict = PyModule_GetDict (extmodule);
        PyObject* extload = PyDict_GetItemString (extdict, "load_extended");
        PyObject* extsave = PyDict_GetItemString (extdict, "save_extended");
        PyDict_SetItemString (dict, "load_extended", extload);
        PyDict_SetItemString (dict, "save_extended", extsave);
        PyDict_SetItemString (dict, "load", extload);
        /* PyDict_SetItemString does an internal INCREF */
        /*
          Py_INCREF(extload);
          Py_INCREF(extsave);
          Py_INCREF(extload);
        */
        Py_DECREF (extmodule);
        st->is_extended = 1;
    }
    else
    {
        PyObject* basicload = PyDict_GetItemString (dict, "load_basic");
        PyErr_Clear ();
        PyDict_SetItemString (dict, "load_extended", Py_None);
        PyDict_SetItemString (dict, "save_extended", Py_None);
        PyDict_SetItemString (dict, "load", basicload);
        /* PyDict_SetItemString does an internal INCREF */
        /*
          Py_INCREF(Py_None);
          Py_INCREF(Py_None);
          Py_INCREF(basicload);
        */
        st->is_extended = 0;
    }
    MODINIT_RETURN (module);
}
